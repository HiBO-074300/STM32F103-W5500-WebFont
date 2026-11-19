/**
  ******************************************************************************
  * @file    http_server.c
  * @brief   极简 HTTP 服务：在 192.168.97.138:80 提供测试页与状态接口
  *
  *          路由：
  *            GET /               -> 测试页
  *            GET /index.html     -> 测试页
  *            GET /api/status     -> JSON 状态
  *            GET /api/font?code=XXXX&size=N -> 字模点阵（code 为 GBK 码）
  *            其它                -> 404
  ******************************************************************************
  */
#include "http_server.h"
#include "w5500_port.h"
#include "Fontlib.h"
#include "main.h"
#include "w5500.h"
#include "socket.h"

#include <stdio.h>
#include <string.h>

#define HTTP_SOCKET     0u
#define HTTP_PORT       80u
#define HTTP_REQ_SIZE   512u
#define HTTP_SEND_CHUNK 1024u

static const char HTTP_PAGE[] =
    "<!DOCTYPE html>\n"
    "<html lang='zh'>\n"
    "<head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>\n"
    "<title>Font Grid Test</title>\n"
    "<style>\n"
    "body{font-family:Arial,sans-serif;background:#10141c;color:#e6edf3;margin:0;padding:24px}\n"
    ".card{max-width:640px;margin:auto;background:#1c2330;border-radius:10px;padding:24px;box-shadow:0 6px 20px rgba(0,0,0,.4)}\n"
    "h1{margin:0 0 10px;font-size:22px}\n"
    ".badge{display:inline-block;background:#2ea043;color:#fff;padding:2px 10px;border-radius:12px;font-size:12px}\n"
    "input,select,button{font-size:15px;padding:6px 10px;margin-top:14px}\n"
    "button{cursor:pointer;background:#238636;color:#fff;border:0;border-radius:6px}\n"
    "table{width:100%;border-collapse:collapse;margin-top:18px}\n"
    "td{padding:7px 0;border-bottom:1px solid #2d3646}\n"
    "td:first-child{color:#8b949e;width:42%}\n"
    "code{color:#79c0ff}\n"
    "#grid{margin-top:16px;display:grid;gap:1px;background:#444;padding:4px;width:max-content}\n"
    ".cell{width:14px;height:14px;background:#fff}\n"
    ".cell.on{background:#000}\n"
    "#msg{color:#f85149;min-height:18px;margin-top:8px}\n"
    "</style></head>\n"
    "<body><div class='card'>\n"
    "<h1>字库点阵测试 / Font Grid</h1>\n"
    "<span class='badge'>W5500 + SPI Flash(F2_CS)</span>\n"
    "<div><input id='ch' maxlength='4' value='中' placeholder='输入汉字'>"
    "<select id='size'><option value='1'>15x16</option><option value='0'>12x12</option><option value='2'>24x24</option><option value='3'>32x32</option></select>"
    "<button onclick='show()'>显示字形</button></div>\n"
    "<div id='msg'></div>\n"
    "<div id='grid'></div>\n"
    "<div style='margin-top:12px;color:#8b949e'>GBK code: <code id='code'>-</code></div>\n"
    "<table>\n"
    "<tr><td>IP Address</td><td>192.168.97.138</td></tr>\n"
    "<tr><td>Requests</td><td id='req'>-</td></tr>\n"
    "<tr><td>Uptime</td><td id='upt'>-</td></tr>\n"
    "<tr><td>Link</td><td id='link'>-</td></tr>\n"
    "</table>\n"
    "</div>\n"
    "<script>\n"
    "function gbkCode(ch){\n"
    "  var dec=new TextDecoder('gbk');\n"
    "  for(var b1=0xA1;b1<=0xF7;b1++)for(var b2=0xA1;b2<=0xFE;b2++){\n"
    "    if(dec.decode(new Uint8Array([b1,b2]))===ch)return ('0000'+((b1<<8)|b2).toString(16).toUpperCase()).slice(-4);\n"
    "  }\n"
    "  return null;\n"
    "}\n"
    "function show(){\n"
    "  var ch=document.getElementById('ch').value.trim().charAt(0);\n"
    "  var msg=document.getElementById('msg');\n"
    "  var code=gbkCode(ch);\n"
    "  if(!code){msg.textContent='无法转换为 GBK 码';return;}\n"
    "  document.getElementById('code').textContent=code;\n"
    "  fetch('/api/font?code='+code+'&size='+document.getElementById('size').value)\n"
    "  .then(function(r){if(!r.ok)throw new Error('HTTP '+r.status);return r.text();}).then(function(t){\n"
    "    var d;try{d=JSON.parse(t);}catch(e){msg.textContent='JSON错误: '+t.slice(0,60);return;}\n"
    "    if(d.error){msg.textContent=d.error;return;}\n"
    "    msg.textContent='';\n"
    "    var g=document.getElementById('grid');g.innerHTML='';\n"
    "    g.style.gridTemplateColumns='repeat('+d.w+',14px)';\n"
    "    var bytes=[];for(var i=0;i<d.data.length;i+=2)bytes.push(parseInt(d.data.substr(i,2),16));\n"
    "    var bpr=Math.ceil(d.w/8);\n"
    "    for(var r=0;r<d.h;r++)for(var c=0;c<d.w;c++){\n"
    "      var b=bytes[r*bpr+Math.floor(c/8)];\n"
    "      var el=document.createElement('div');\n"
    "      el.className='cell'+(((b&(0x80>>(c%8)))!==0)?' on':'');\n"
    "      g.appendChild(el);\n"
    "    }\n"
    "  }).catch(function(e){msg.textContent='请求失败: '+(e&&e.message?e.message:e);});\n"
    "}\n"
    "function refresh(){fetch('/api/status').then(function(r){return r.json();}).then(function(d){\n"
    "  document.getElementById('req').textContent=d.requests;\n"
    "  document.getElementById('upt').textContent=d.uptime+' s';\n"
    "  document.getElementById('link').textContent=d.link?'Up':'Down';\n"
    "});}\n"
    "refresh();setInterval(refresh,1000);\n"
    "</script></body></html>\n";

static const char JSON_NOT_FOUND[] = "{\"error\":\"not found\"}";
static const char JSON_BAD_METHOD[] = "{\"error\":\"method not allowed\"}";

static uint8_t  s_req[HTTP_REQ_SIZE + 1];
static uint32_t s_requests;
static uint32_t s_start_tick;

/* 分块发送，避免超过 W5500 单次 TX 缓冲 */
static void http_send(const char *data, uint16_t len)
{
    uint16_t offset = 0;

    while (offset < len)
    {
        uint16_t chunk = len - offset;
        int32_t sent;
        uint32_t tick;

        if (chunk > HTTP_SEND_CHUNK)
        {
            chunk = HTTP_SEND_CHUNK;
        }

        tick = HAL_GetTick();
        for (;;)
        {
            sent = send(HTTP_SOCKET, (uint8_t *)(void *)(data + offset), chunk);
            if (sent > 0)
            {
                break;
            }
            if (sent < 0)
            {
                return;
            }
            /* SOCK_BUSY(0)：上一包 SEND_OK 尚未到达，等待后重试 */
            if ((HAL_GetTick() - tick) >= 2000u)
            {
                return;
            }
        }
        offset += chunk;
    }
}

static void http_respond(const char *status, const char *content_type, const char *body)
{
    char header[160];
    int len = snprintf(header, sizeof(header),
                       "HTTP/1.1 %s\r\n"
                       "Content-Type: %s\r\n"
                       "Content-Length: %u\r\n"
                       "Connection: close\r\n"
                       "Cache-Control: no-store\r\n"
                       "\r\n",
                       status, content_type, (unsigned)strlen(body));

    if (len > 0)
    {
        uint32_t tick;

        http_send(header, (uint16_t)len);
        http_send(body, (uint16_t)strlen(body));

        /* 等待最后一包发送完成，再断开连接 */
        tick = HAL_GetTick();
        while (((getSn_IR(HTTP_SOCKET) & Sn_IR_SENDOK) == 0u) &&
               ((HAL_GetTick() - tick) < 500u))
        {
        }
        setSn_IR(HTTP_SOCKET, Sn_IR_SENDOK);
    }
}

/*=========================== 字模接口辅助函数 ==============================*/
static const char HTTP_HEX[] = "0123456789ABCDEF";

static int http_hex_digit(char c)
{
    if ((c >= '0') && (c <= '9')) return (c - '0');
    if ((c >= 'A') && (c <= 'F')) return (c - 'A' + 10);
    if ((c >= 'a') && (c <= 'f')) return (c - 'a' + 10);
    return -1;
}

static int http_parse_code(const char *s, uint16_t *code)
{
    uint16_t value = 0u;
    uint8_t i;

    if (s == NULL) return 0;
    for (i = 0u; i < 4u; i++)
    {
        int h = http_hex_digit(s[i]);
        if (h < 0) return 0;
        value = (uint16_t)((value << 4) | (uint16_t)h);
    }
    *code = value;
    return 1;
}

static void http_font_response(const char *query)
{
    char body[340];
    char *code_pos = strstr((char *)query, "code=");
    char *size_pos = strstr((char *)query, "size=");
    uint8_t font[128];
    uint16_t code = 0u;
    uint8_t width = 16u;
    uint8_t height = 16u;
    uint8_t bytes_len = 32u;
    uint8_t size_idx = 1u;
    uint8_t msb;
    uint8_t lsb;
    uint8_t i;
    size_t off;

    if ((code_pos == NULL) || (http_parse_code(code_pos + 5, &code) == 0))
    {
        http_respond("400 Bad Request", "application/json", "{\"error\":\"invalid code\"}");
        return;
    }

    if (size_pos != NULL)
    {
        int s = http_hex_digit(size_pos[5]);
        if ((s >= 0) && (s <= 3)) size_idx = (uint8_t)s;
    }

    if (code < 0x80u)
    {
        width = 8u;
        height = 16u;
        bytes_len = 16u;
        (void)Read_One_Char((uint8_t)code, font, CHAR_SIZE_8x16);
    }
    else
    {
        msb = (uint8_t)(code >> 8);
        lsb = (uint8_t)(code & 0xFFu);
        if ((((msb >= 0xA1u) && (msb <= 0xA9u)) || ((msb >= 0xB0u) && (msb <= 0xF7u))) &&
            (lsb >= 0xA1u))
        {
            switch (size_idx)
            {
                case 0u:
                    width = 12u; height = 12u; bytes_len = 24u;
                    (void)Read_One_Font(code, font, FONT_SIZE_12x12);
                    break;
                case 2u:
                    width = 24u; height = 24u; bytes_len = 72u;
                    (void)Read_One_Font(code, font, FONT_SIZE_24x24);
                    break;
                case 3u:
                    width = 32u; height = 32u; bytes_len = 128u;
                    (void)Read_One_Font(code, font, FONT_SIZE_32x32);
                    break;
                default:
                    (void)Read_One_Font(code, font, FONT_SIZE_15x16);
                    break;
            }
        }
        else
        {
            http_respond("400 Bad Request", "application/json", "{\"error\":\"code out of range\"}");
            return;
        }
    }

    off = (size_t)snprintf(body, sizeof(body),
                           "{\"code\":\"%04X\",\"w\":%u,\"h\":%u,\"data\":\"",
                           (unsigned)code, (unsigned)width, (unsigned)height);
    for (i = 0u; (i < bytes_len) && ((off + 2u) < (sizeof(body) - 1u)); i++)
    {
        body[off++] = HTTP_HEX[font[i] >> 4];
        body[off++] = HTTP_HEX[font[i] & 0x0Fu];
    }
    if ((off + 2u) < sizeof(body))
    {
        body[off++] = '\"';
        body[off++] = '}';
    }
    body[off] = '\0';

    http_respond("200 OK", "application/json", body);
}

static void http_handle_request(void)
{
    char *path;
    char *end;
    char *query;
    char body[128];
    int len;

    s_requests++;

    if (strncmp((char *)s_req, "GET ", 4) != 0)
    {
        http_respond("405 Method Not Allowed", "application/json", JSON_BAD_METHOD);
        return;
    }

    path = (char *)s_req + 4;
    end = strchr(path, ' ');
    if (end != NULL)
    {
        *end = '\0';
    }
    query = strchr(path, '?');
    if (query != NULL)
    {
        *query = '\0';
        query++;
    }

    if ((strcmp(path, "/") == 0) || (strcmp(path, "/index.html") == 0))
    {
        http_respond("200 OK", "text/html; charset=utf-8", HTTP_PAGE);
        return;
    }

    if (strcmp(path, "/api/status") == 0)
    {
        len = snprintf(body, sizeof(body),
                       "{\"ip\":\"192.168.97.138\",\"requests\":%lu,\"uptime\":%lu,\"link\":%u}",
                       (unsigned long)s_requests,
                       (unsigned long)((HAL_GetTick() - s_start_tick) / 1000u),
                       (unsigned)w5500_get_link());
        http_respond("200 OK", "application/json", (len > 0) ? body : JSON_NOT_FOUND);
        return;
    }

    if (strcmp(path, "/api/font") == 0)
    {
        http_font_response((query != NULL) ? query : "");
        return;
    }

    http_respond("404 Not Found", "application/json", JSON_NOT_FOUND);
}

void http_server_init(void)
{
    s_requests = 0u;
    s_start_tick = HAL_GetTick();
}

void http_server_poll(void)
{
    uint8_t status = getSn_SR(HTTP_SOCKET);

    switch (status)
    {
        case SOCK_CLOSED:
            if (socket(HTTP_SOCKET, Sn_MR_TCP, HTTP_PORT, 0) == HTTP_SOCKET)
            {
                listen(HTTP_SOCKET);
            }
            break;

        case SOCK_INIT:
            listen(HTTP_SOCKET);
            break;

        case SOCK_CLOSE_WAIT:
            if (getSn_RX_RSR(HTTP_SOCKET) == 0u)
            {
                disconnect(HTTP_SOCKET);
            }
            break;

        case SOCK_ESTABLISHED:
        {
            uint16_t ready = getSn_RX_RSR(HTTP_SOCKET);

            if (ready > 0u)
            {
                uint16_t len = (ready > HTTP_REQ_SIZE) ? HTTP_REQ_SIZE : ready;

                if (recv(HTTP_SOCKET, s_req, len) > 0)
                {
                    s_req[len] = '\0';
                    http_handle_request();
                }
                disconnect(HTTP_SOCKET);
            }
            else if ((getSn_IR(HTTP_SOCKET) & Sn_IR_TIMEOUT) != 0u)
            {
                setSn_IR(HTTP_SOCKET, Sn_IR_TIMEOUT);
                disconnect(HTTP_SOCKET);
            }
            break;
        }

        default:
            break;
    }
}
