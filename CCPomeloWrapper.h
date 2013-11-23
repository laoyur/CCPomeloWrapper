//
//  CCPomeloWrapper.h
//  SGXXZ
//
//  Created by laoyur@126.com on 13-11-22.
//
//  A simple libpomelo wrapper for cocos2d-x

#ifndef __CCPomeloWrapper__
#define __CCPomeloWrapper__

#include "cocos2d.h"

enum CCPomeloStatus
{
    EPomeloStopped,
    EPomeloConnecting,
    EPomeloConnected
};


class CCPomeloImpl;

class CCPomeloRequestResult
{
public:
    int status;         //status code
    std::string requestRoute;
    std::string jsonMsg;
    
private:
    CCPomeloRequestResult();
    friend class CCPomeloImpl;
};

class CCPomeloNotifyResult
{
public:
    int status;
    std::string notifyRoute;
    
private:
    CCPomeloNotifyResult();
    friend class CCPomeloImpl;
};

class CCPomeloEvent
{
public:
    std::string event;
    std::string jsonMsg;
private:
    CCPomeloEvent();
    friend class CCPomeloImpl;
};

typedef void (cocos2d::CCObject::*PomeloAsyncConnHandler)(int);
typedef void (cocos2d::CCObject::*PomeloReqResultHandler)(const CCPomeloRequestResult&);
typedef void (cocos2d::CCObject::*PomeloNtfResultHandler)(const CCPomeloNotifyResult&);
typedef void (cocos2d::CCObject::*PomeloEventHandler)(const CCPomeloEvent&);

#define pomelo_async_conn_cb_selector(_SEL) (PomeloAsyncConnHandler)(&_SEL)
#define pomelo_req_result_cb_selector(_SEL) (PomeloReqResultHandler)(&_SEL)
#define pomelo_ntf_result_cb_selector(_SEL) (PomeloNtfResultHandler)(&_SEL)
#define pomelo_listener_cb_selector(_SEL) (PomeloEventHandler)(&_SEL)


class CCPomeloWrapper : public cocos2d::CCObject
{
public:
    static CCPomeloWrapper* getInstance();
    ~CCPomeloWrapper();
    
public:
    //get current connection status
    CCPomeloStatus status() const;
    
    //@return: 0--connect succeeded; others--connect failed
    int connect(const char* host, int port);
    
    //@return: 0--connect request succeeded; others--connect request failed
    int connectAsnyc(const char* host, int port, cocos2d::CCObject* pCallbackTarget, PomeloAsyncConnHandler pCallbackSelector);
    
    //stop current connection
    void stop();
    
    //send request to server
    //@return: 0--request sent succeeded; others--request sent failed
    int request(const char* route, const std::string& msg, cocos2d::CCObject* pCallbackTarget, PomeloReqResultHandler pCallbackSelector);
    
    //send notify to server
    //@return: 0--notify sent succeeded; others--notify sent failed
    int notify(const char* route, const std::string& msg, cocos2d::CCObject* pCallbackTarget, PomeloNtfResultHandler pCallbackSelector);
  
    //listen to event
    //@return: 0--add listener succeeded; others--add listener failed
    int addListener(const char* event, cocos2d::CCObject* pCallbackTarget, PomeloEventHandler pCallbackSelector);
    
private:
    CCPomeloWrapper();
    
private:
    CCPomeloImpl*   _theMagic;
    friend class CCPomeloImpl;
};

#endif /* defined(__CCPomeloWrapper__) */
