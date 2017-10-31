# Web-Server
Basic multi-threaded Web Server implementing compression and range requests serving files from the `public_html` directory. Compression is implemented through the [zlib](https://zlib.net/zlib_how.html) library and range requests can be done by modifying the Range header as follows:
```
curl http://www.example.com -i -H "Range: bytes=0-100"
```
