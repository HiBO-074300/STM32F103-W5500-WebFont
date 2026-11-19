/**
  ******************************************************************************
  * @file    http_server.h
  * @brief   基于 W5500 socket0 的简易 HTTP 测试页服务
  *          访问地址：http://192.168.97.138/
  ******************************************************************************
  */
#ifndef __HTTP_SERVER_H
#define __HTTP_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 初始化计数器（socket 由轮询中按需打开） */
void http_server_init(void);

/** @brief 在主循环中周期调用，处理连接与请求 */
void http_server_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* __HTTP_SERVER_H */
