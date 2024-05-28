# QDPlay
Утилита для организации моста между потоковым видео и ГУ автомобиля оснащенного приложением QDLink

> [!WARNING]
> Вы используетет данное програмное обеспечение на свой страх и риск. Авторы данного ПО не несут ответственности за любой ущерб, причененный данным ПО!

## Поддержываемые ГУ
Как таковых ограничейний на поддержку сейчас нету, главное чтобы на ГУ был установлен QDLink. Единственный момент - разрешение ГУ. Пока не реализован механизм автоматического определения разрешения, поэтому в релизах находятся следующие образы:
- `opi-zero-2w-сarplay-belgee-x50-new-hu-1760x720.img` - Образ 1760х720 (Belgee x50 c ГУ на Android 9)
- `opi-zero-2w-сarplay-geely-old-hu-1920x590.img` - Образ 1920x590 (Coolray и x50 на Android 4.3)

Для других ГУ разрешение нужно менять вручную. Это можно сделать либо через терминал отладочного вывода orange pi, либо на компьютере с поддержкой файловой системы `ext4`.

Редактирование производится в файле:
```sh
 vim /lib/systemd/system/carplay.service
```

Содержание файла:
```ini
[Unit]
Description=Apple CarPlay
After=mdnsd.service

[Service]
Type=simple
ExecStart=/usr/sbin/carplay --width <ширина> --height <высота>
KillSignal=SIGINT
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```
Вместо `<ширина>` и `<высота>` вам нужно вставить свои значения.

## MFI
Для работы CarPlay вам необходимо припаять чип `mfi337s3959` к шине `i2c` согласно [схеме](hardware/opi-zero-2w/orange-pi-zero-2w-mfi.pdf). Самый простой способ это сделать - заказать на производстве платы по данным [Gerber файлам](hardware/opi-zero-2w/orange-pi-zero-2w-mfi-gerber.zip), и собрать ее. 
Так же доступна [3D модель](hardware/opi-zero-2w/case/) корпуса, готовая к печати на 3D принтере.

## Bluetooth
Встроенный `Bluetooth` у orange pi zero 2w имеет деффект (телефон подключсается только со второго раза) либо программный, либо аппаратный, поэтому на плате упомянутой выше добавлен дополнительный `USB` разъем для внешнего `Bluetooth` адаптера TP-LINK UB500. 
Чтобы `USB` порт функционировал,  `mfi` плату необходимо соединить шлейфом с `orange pi zero 2w`. При этом шлейф необходим прямой , 25 контактов с шагом 0.5 мм.

Если вас устраивает деффект, то `USB` и его обвязку можно не припаивать.

## Подключение
- Подключите устройство к ГУ
- Запустите QDLink
- Подключитесь к устройству по bluetooth со смартфона
- Подключителс к ГУ по bluetooth со смартфона, так же в ГУ выберите, чтобы источником звука был ваш смартфон

# Для разработчиков
## Важно
Крайне не рекомендуется выводить на ГУ, что-то кроме автомобильных ассистентов!

## Вывод видео
Для начала определите следующие типы данных:

```C
// Сушность экрана
struct qd_screen_impl {
    int sink_fd;
    uint8_t* buffer;
    size_t buffer_size;
    uint32_t width;
	  uint32_t height;
	  uint32_t frame_rate;
};
typedef struct qd_screen_impl* qd_screen_ref;

// Заголовок протокола предачи видео

typedef struct {
    uint32_t mark;
    uint32_t event_id;
    uint32_t full_len;
    uint32_t payload_len;
} __attribute__((packed)) qd_sceen_parcel_header_t;

// Событие предачи h264 фрейма

typedef struct {
	  uint32_t width;
    uint32_t height;
    uint32_t frame_rate;
    uint32_t frame_size;
} __attribute__((packed)) qd_sceen_parcel_video_event_t;

// Комбинированный заголовок

typedef struct {
    qd_sceen_parcel_header_t header;
    qd_sceen_parcel_video_event_t event;
} __attribute__((packed)) qd_screen_video_frame_t;
```

Далее подключитесь к локальному сокету QDPlay:
```C
#include "qd_video_sender.h"
...
qd_screen_impl_ref screen = qd_screen_connect(video_width, video_height, frame_rate);
```

Тут:
  - `video_width` - Ширина видео потока;
  - `video_height` - Высота видео потока;
  - `frame_rate` - Частота кадров видео потока.

После подключения, на экране ГУ появится прогресс бар. Устройство готово к приему видео.
> [!IMPORTANT]
> Видео поток необходимо начинать отправлять сразу же после подключения!

Отправка видео потока:
```C
int ret = qd_screen_send_data(
    screen, // подключение к экрану
    (const uint8_t*)data, // h264 фрейм, начинающийсы с 0x00 0x00 0x00 0x01
    (size_t)len // размер фрейма в байтах
);
```

Если неободимо завершить трансляцию, то необходимо удалить сущность экрана и отключиться от сокета:
```C
qd_screen_close(screen);
```

Исходные коды данного вещателя можно найти [тут](video-sender).

## Получение событий

Для начала нужно присоединиться к сокету событий:
```C
int status = qd_input_reconnect();
```
> [!IMPORTANT]
> Переподключаться нужно до тех пор, пока `status` не станет равным нулю!

После успешного подключения вам нужно получать события при помощи вызова:
```C

static int _touch_screen_update(bool press, uint16_t x, uint16_t y) {
    // Обработка касания
    return 0;
}

static int _set_needs_key_frame(void) {
    // Запрос ключевого кадра у h264 провайдера
    return 0;
}

int main(int argc, char ** argv) {
    if (qd_input_reconnect() < 0){
        return -1;
    }

    while (1)
    {
        int result = qd_input_process(
            _touch_screen_update,
            _set_needs_key_frame
        );

        if (result == 0) {
            continue;
        }

        if (qd_input_reconnect() < 0) {
            break;
        }
    }

    return -1;
}


Исходные коды данного обработчика можно найти [тут](control-receiver).
```







