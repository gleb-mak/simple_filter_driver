# simple_filter_driver
Драйвер фильтр для Windows для ps2 мыши, который включает и отключает инверсию оси Y при последовательном нажатии левой, правой, левой кнопок мыши. 

Для установки нужно:
1) отключить проверку подписи драйверов(например нажать F8 при запуске системы и выбрать соответствующий параметр).
2)зайти в командную строку от имени администратора, и прописать 
```bash
> sc create Name binPath= "path_to_Mouse.sys" type= kernel
```
Для загрузки и выгрузки драйвера используйте следующие команды:
```bash
> sc start Name
> sc stop Name
```
