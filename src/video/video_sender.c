#include <stdlib.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <netinet/in.h>
#include <stdio.h>
#include "video/video_sender.h"
#include "messages/screen_header.h"

#pragma mark - Private propoerties

pthread_mutex_t video_sender_mutex;
pthread_t video_sender_thread_id = 0;
int video_sender_accessory_fd = 0;
int video_sender_is_running = 0;

#pragma mark - Private methods definitions

void * video_sender_thread(void * arg);
int video_sender_find_next_frame(int fd);

#pragma mark - Internal methods implementation

void video_sender_start(int fd) {

    pthread_mutex_lock(&video_sender_mutex);

    if (video_sender_is_running) {
        printf("[Warning] Video thread already started!\n");
        pthread_mutex_unlock(&video_sender_mutex);

        return;
    }

    int result = pthread_create(
        &video_sender_thread_id,
        NULL,
        video_sender_thread,
        NULL
    );

    if (result != 0) {
        printf("[Error] can`t start video thread!\n");
        pthread_mutex_unlock(&video_sender_mutex);

        video_sender_accessory_fd = -1;

        return;
    }

    video_sender_accessory_fd = fd;
    video_sender_is_running = 1;

    pthread_mutex_unlock(&video_sender_mutex);
}

void video_sender_await_completion(void) {
    if (video_sender_is_running == 0) {
        return;
    }

    pthread_join(video_sender_thread_id, NULL);
}

void video_sender_set_needs_sps(void) {
    
}

#pragma mark - Private methods implementation

void * video_sender_thread(void * arg) {
    
    int input_h264 = open("input.h264", O_RDONLY);

    while (video_sender_is_running && video_sender_accessory_fd >= 0 && input_h264 >= 0) {
        int capture_data_len = video_sender_find_next_frame(input_h264);
		
        if (capture_data_len <= 0) {
            break;
        }

        uint32_t header_size = 512;
        uint32_t chunked_size = (capture_data_len / 512) * 512;

        if (chunked_size < capture_data_len) {
            chunked_size += 512;
        }

        uint32_t total_size = header_size + chunked_size;
		uint8_t* screen_response = (uint8_t*)malloc(total_size);

        if (!screen_response) {
            break;
        }

        memset(screen_response, 0, total_size);

        screen_header_t * screen_header = (screen_header_t*)screen_response;
        screen_header_response_init(screen_header, 1920, 590, 30, 4);

        screen_header->common_header.total_size = ntohl(total_size);
        screen_header->capture_data_len = ntohl(capture_data_len);
			
        if (read(input_h264, screen_response + header_size, capture_data_len) <= 0) {
            free(screen_response);
            break;
        }
        
        if (write(video_sender_accessory_fd, screen_response, total_size) <= 0) {
            free(screen_response);
            break;
        }

        free(screen_response);
    }
    
    pthread_mutex_lock(&video_sender_mutex);
    video_sender_is_running = 0;
    pthread_mutex_unlock(&video_sender_mutex);
}

int video_sender_find_next_frame(int fd) {
    const uint8_t h264_mark[] = { 0x00, 0x00, 0x00, 0x01 };
    uint8_t buffer[4] = { 0 };
    
    int readed_len = 0;
    
    while (1) {
        memmove(buffer, buffer + 1, 3);

        if (read(fd, buffer + 3, 1) <= 0) {
            break;
        }

        if (++readed_len == sizeof(h264_mark) && memcmp(buffer, h264_mark, 4) != 0) {
            readed_len--;
            continue;
        }

        if (readed_len > sizeof(h264_mark) && memcmp(buffer, h264_mark, 4) == 0) {
            lseek(fd, -readed_len, SEEK_CUR);
			readed_len -= sizeof(h264_mark);

            break;
        }
    }
    
    return readed_len;
}