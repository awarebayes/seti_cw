# Курсовая работа Щербины МА

Номер по ЭУ: 19 
19 % 8 + 1 = 3, thread pool + pselect()

### Features
- GET / HEAD HTTP 1.1 with reusable connections
- URL Parsing: ./aboba.txt?biba=boba#aaaa
- URL Parsing with escape: `http://localhost/%D0%BA%D0%BE%D1%82%D0%B5%D0%BD%D0%BE%D0%BA.jpg`
- URL Parsing: http://localhost/%D0%BA%D0%BE%D1%82%D0%B5%D0%BD%D0%BE%D0%BA.jpg?asd=sdfdds#dsfdsf 
- File directory listing with `<a>` navigation (configurable)
- 304 Not Changed Support
- 403 Forbidden Support
- Range header suppport
- Mime support: js,css, html, images, videos, flash + add your own in config
- Chroot with priveleges deescalation
- Logging: info, warn, die (critical)
- Large file transfer: 10GB tested
- Both epoll + pselect implemented
- Multithreading with slots

