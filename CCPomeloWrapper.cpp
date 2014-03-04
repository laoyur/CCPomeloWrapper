//
//  CCPomeloWrapper.cpp
//
//  Created by laoyur@126.com on 13-11-22.
//
//  A simple libpomelo wrapper for cocos2d-x

#include "CCPomeloWrapper.h"
#include <errno.h>
#include <queue>
#include "pomelo.h"
#include "jansson.h"

using namespace std;
USING_NS_CC;

static CCPomeloWrapper* gPomelo = NULL;

struct _PomeloUser
{
#if CCX3
    _PomeloUser(){ connCB = NULL; reqCB = NULL; ntfCB = NULL; evtCB = NULL; };
    ~_PomeloUser(){};

    PomeloAsyncConnCallback connCB; //for async conn
    PomeloReqResultCallback reqCB;  //for request
    PomeloNtfResultCallback ntfCB;  //for notify
    PomeloEventCallback evtCB;      //for listener

#else
    CCObject* target;   //by ref
    union
    {
        PomeloAsyncConnHandler connSel; //for async conn
        PomeloReqResultHandler reqSel;  //for request
        PomeloNtfResultHandler ntfSel;  //for notify
        PomeloEventHandler evtSel;      //for listener
    };
#endif

};

struct _PomeloRequestResult
{
    pc_request_t* request;  //by ref
    int status;
    string resp;
};

struct _PomeloNotifyResult
{
    pc_notify_t* notify;    //by ref
    int status;
};

struct _PomeloEvent
{
    string event;
    string data;
};

CCPomeloRequestResult::CCPomeloRequestResult()
{
}
CCPomeloNotifyResult::CCPomeloNotifyResult()
{
}
CCPomeloEvent::CCPomeloEvent()
{
}

class CCPomeloImpl : 
#if CCX3
public cocos2d::Object
#else
public cocos2d::CCObject
#endif
{
    
public:
    CCPomeloImpl();
    virtual ~CCPomeloImpl();
    
    CCPomeloStatus status() const;
    
    int connect(const char* host, int port);
    
#if CCX3
    int connectAsnyc(const char* host, int port, const PomeloAsyncConnCallback& callback);
    
    int setDisconnectedCallback(const std::function<void()>& callback);
    
    int request(const char* route, const std::string& msg, const PomeloReqResultCallback& callback);
    
    int notify(const char* route, const std::string& msg, const PomeloNtfResultCallback& callback);
    
    int addListener(const char* event, const PomeloEventCallback& callback);
#else
    int connectAsnyc(const char* host, int port, cocos2d::CCObject* pCallbackTarget, PomeloAsyncConnHandler pCallbackSelector);

    int setDisconnectedCallback(cocos2d::CCObject* pTarget, cocos2d::SEL_CallFunc pSelector);
    
    int request(const char* route, const std::string& msg, cocos2d::CCObject* pCallbackTarget, PomeloReqResultHandler pCallbackSelector);
    
    int notify(const char* route, const std::string& msg, cocos2d::CCObject* pCallbackTarget, PomeloNtfResultHandler pCallbackSelector);
    
    int addListener(const char* event, cocos2d::CCObject* pCallbackTarget, PomeloEventHandler pCallbackSelector);

#endif
    
    void stop();
    void removeListener(const char* event);
    void removeAllListeners();
    
private:
    //callbacks for libpomelo
    static void connectAsnycCallback(pc_connect_t* conn_req, int status);
    static void requestCallback(pc_request_t *request, int status, json_t *docs);
    static void notifyCallback(pc_notify_t *ntf, int status);
    static void eventCallback(pc_client_t *client, const char *event, void *data);
    static void disconnectedCallback(pc_client_t *client, const char *event, void *data);
    
private:
    void ccDispatcher(float delta);
    void dispatchAsyncConnCallback();
    void dispatchRequestCallbacks();
    void dispatchNotifyCallbacks();
    void dispatchEventCallbacks();
    
    void pushReqResult(_PomeloRequestResult* reqResult);
    void pushNtfResult(_PomeloNotifyResult* ntfResult);
    void pushEvent(_PomeloEvent* event);
    
    _PomeloRequestResult* popReqResult(bool lock = true);
    _PomeloNotifyResult* popNtfResult(bool lock = true);
    _PomeloEvent* popEvent(bool lock = true);
    
    void clearReqResource();
    void clearNtfResource();
    void clearAllPendingEvents();
    
    void lock();
    void unlock();
    
private:
    CCPomeloStatus      mStatus;
    pc_client_t*        mClient;
    
    _PomeloUser*            mAsyncConnUser;
    bool                    mAsyncConnDispatchPending;
    int                     mAsyncConnStatus;
    pc_connect_t*       mAsyncConn; //by ref
    
    pthread_mutex_t     mMutex;
#if CCX3
    std::function<void()> mDisconnectCB;
#else
    CCObject*           mDisconnectCbTarget;
    SEL_CallFunc        mDisconnectCbSelector;
#endif
    
    map<pc_request_t*,_PomeloUser*> mReqUserMap;
    queue<_PomeloRequestResult*> mReqResultQueue;
    
    map<string,_PomeloUser*> mEventUserMap;
    queue<_PomeloEvent*> mEventQueue;
    
    map<pc_notify_t*,_PomeloUser*> mNtfUserMap;
    queue<_PomeloNotifyResult*> mNtfResultQueue;
};

void CCPomeloImpl::ccDispatcher(float delta)
{
    dispatchAsyncConnCallback();
    dispatchRequestCallbacks();
    dispatchNotifyCallbacks();
    dispatchEventCallbacks();
}
void CCPomeloImpl::dispatchAsyncConnCallback()
{
    if(mAsyncConnDispatchPending)
    {
        mAsyncConnDispatchPending = false;
        
        pc_add_listener(mClient, PC_EVENT_DISCONNECT, disconnectedCallback);
        
        _PomeloUser* user = mAsyncConnUser;
#if CCX3
        if(user && user->connCB)
        {
            user->connCB(mAsyncConnStatus);
        }
#else
        if(user && user->target && user->connSel)
        {
            PomeloAsyncConnHandler sel = user->connSel;
            (user->target->*sel)(mAsyncConnStatus);
        }
#endif
        
        if(mAsyncConnUser == user)  //just in case of stop() called in cb
        {
            delete mAsyncConnUser;
            mAsyncConnUser = NULL;
        }
    }
}
void CCPomeloImpl::dispatchRequestCallbacks()
{
    _PomeloRequestResult* rst = popReqResult(mStatus == EPomeloConnected);
    if(rst)
    {
        _PomeloUser* user = NULL;
        
        if(mReqUserMap.find(rst->request) != mReqUserMap.end())
        {
            user = mReqUserMap[rst->request];
            mReqUserMap.erase(rst->request);
        }
#if CCX3
        //here is the good place to perform callback
        if(user && user->reqCB)
        {
            CCPomeloRequestResult result;
            result.requestRoute = rst->request->route;
            result.status = rst->status;
            result.jsonMsg = rst->resp;
            
            user->reqCB(result);
        }
#else
        //here is the good place to perform callback
        if(user && user->target && user->reqSel)
        {
            CCPomeloRequestResult result;
            result.requestRoute = rst->request->route;
            result.status = rst->status;
            result.jsonMsg = rst->resp;
            
            PomeloReqResultHandler sel = user->reqSel;
            (user->target->*sel)(result);
        }
#endif

        delete user;
        
        //fixme
        json_decref(rst->request->msg);
        pc_request_destroy(rst->request);
        delete rst;
    }
}
void CCPomeloImpl::dispatchNotifyCallbacks()
{
    _PomeloNotifyResult* rst = popNtfResult(mStatus == EPomeloConnected);
    if(rst)
    {
        _PomeloUser* user = NULL;
        
        if(mNtfUserMap.find(rst->notify) != mNtfUserMap.end())
        {
            user = mNtfUserMap[rst->notify];
            mNtfUserMap.erase(rst->notify);
        }
#if CCX3
        if(user && user->ntfCB)
        {
            CCPomeloNotifyResult result;
            result.notifyRoute = rst->notify->route;
            result.status = rst->status;
            
            user->ntfCB(result);
        }
#else
        if(user && user->target && user->ntfSel)
        {
            CCPomeloNotifyResult result;
            result.notifyRoute = rst->notify->route;
            result.status = rst->status;
            
            PomeloNtfResultHandler sel = user->ntfSel;
            (user->target->*sel)(result);
        }
#endif
        delete user;
        
        //fixme
        json_decref(rst->notify->msg);
        pc_notify_destroy(rst->notify);
        delete rst;
    }
}
void CCPomeloImpl::dispatchEventCallbacks()
{
    _PomeloEvent* rst = popEvent(mStatus == EPomeloConnected);
    if(rst)
    {
        if(rst->event.compare(PC_EVENT_DISCONNECT) == 0)
        {
            //connection lost
            stop(); //release data
            
#if CCX3
            if (mDisconnectCB) {
                mDisconnectCB();
            }
#else
            if(mDisconnectCbTarget && mDisconnectCbSelector)
            {
                (mDisconnectCbTarget->*mDisconnectCbSelector)();
            }
#endif

        }
        else    //for customized events
        {
            _PomeloUser* user = NULL;
            
            if(mEventUserMap.find(rst->event) != mEventUserMap.end())
            {
                user = mEventUserMap[rst->event];
            }
            
#if CCX3
            if(user && user->evtCB)
            {
                CCPomeloEvent result;
                result.event = rst->event;
                result.jsonMsg = rst->data;
                
                user->evtCB(result);
            }
#else
            if(user && user->target && user->evtSel)
            {
                CCPomeloEvent result;
                result.event = rst->event;
                result.jsonMsg = rst->data;
                
                PomeloEventHandler sel = user->evtSel;
                (user->target->*sel)(result);
            }
#endif
            

        }
        delete rst;
    }
}

void CCPomeloImpl::pushReqResult(_PomeloRequestResult* reqResult)
{
    mReqResultQueue.push(reqResult);
}
void CCPomeloImpl::pushNtfResult(_PomeloNotifyResult* ntfResult)
{
    mNtfResultQueue.push(ntfResult);
}
void CCPomeloImpl::pushEvent(_PomeloEvent* event)
{
    mEventQueue.push(event);
}

_PomeloRequestResult* CCPomeloImpl::popReqResult(bool lock/* = true*/)
{
    _PomeloRequestResult* rst = NULL;
    if(lock)
        pthread_mutex_lock(&mMutex);
    if (mReqResultQueue.size() > 0)
    {
        rst = mReqResultQueue.front();
        mReqResultQueue.pop();
    }
    if(lock)
        pthread_mutex_unlock(&mMutex);
    return rst;
}
_PomeloNotifyResult* CCPomeloImpl::popNtfResult(bool lock/* = true*/)
{
    _PomeloNotifyResult* rst = NULL;
    if(lock)
        pthread_mutex_lock(&mMutex);
    if (mNtfResultQueue.size() > 0)
    {
        rst = mNtfResultQueue.front();
        mNtfResultQueue.pop();
    }
    if(lock)
        pthread_mutex_unlock(&mMutex);
    return rst;
}
_PomeloEvent* CCPomeloImpl::popEvent(bool lock/* = true*/)
{
    _PomeloEvent* evt = NULL;
    if(lock)
        pthread_mutex_lock(&mMutex);
    if (mEventQueue.size() > 0)
    {
        evt = mEventQueue.front();
        mEventQueue.pop();
    }
    if(lock)
        pthread_mutex_unlock(&mMutex);
    return evt;
}

void CCPomeloImpl::connectAsnycCallback(pc_connect_t* conn_req, int status)
{
    pthread_mutex_lock(&gPomelo->_theMagic->mMutex);
    
    if(gPomelo->_theMagic->mAsyncConn != conn_req)
    {
        //conn_req是一个已经被“断开”的连接。
        //destory the connection
        pc_client_t* client = conn_req->client;
        pc_connect_req_destroy(conn_req);
        
        //fixme: pomelo forum guys said that
        //we should not use pc_client_destory in worker thread
        pc_client_stop(client);
    }
    else
    {
        pc_connect_req_destroy(conn_req);
        
        gPomelo->_theMagic->mStatus = EPomeloConnected;
        gPomelo->_theMagic->mAsyncConnStatus = status;
        gPomelo->_theMagic->mAsyncConnDispatchPending = true;
        gPomelo->_theMagic->mAsyncConn = NULL;
        
#if CCX3
        CCDirector::getInstance()->getScheduler()->resumeTarget(gPomelo->_theMagic);
#else
        CCDirector::sharedDirector()->getScheduler()->resumeTarget(gPomelo->_theMagic);
#endif
        
    }
    
    pthread_mutex_unlock(&gPomelo->_theMagic->mMutex);
}

void CCPomeloImpl::requestCallback(pc_request_t *request, int status, json_t *docs)
{
    if(gPomelo->status() == EPomeloStopping)
    {
        /*
         EPomeloStopping时，表示此函数是由pc_client_destory()内部触发。
         此时处于主线程中。request会由libpomelo内部进行释放。
         */
        if(gPomelo->_theMagic->mReqUserMap.find(request) != gPomelo->_theMagic->mReqUserMap.end())
        {
            char* json = json_dumps(docs, JSON_COMPACT);    //json is NULL
            _PomeloUser* user = gPomelo->_theMagic->mReqUserMap[request];
            gPomelo->_theMagic->mReqUserMap.erase(request);
            
#if CCX3
            //here is the good place to perform callback
            if(user && user->reqCB)
            {
                CCPomeloRequestResult result;
                result.requestRoute = request->route;
                result.status = status;
                result.jsonMsg = json ? json : "";
                
                user->reqCB(result);
            }
#else
            //here is the good place to perform callback
            if(user && user->target && user->reqSel)
            {
                CCPomeloRequestResult result;
                result.requestRoute = request->route;
                result.status = status;
                result.jsonMsg = json ? json : "";
                
                PomeloReqResultHandler sel = user->reqSel;
                (user->target->*sel)(result);
            }
#endif
            
            delete user;
            
            //fixme
            json_decref(request->msg);
            
            free(json);
        }
    }
    else    //EPomeloConnected
    {
        pthread_mutex_lock(&gPomelo->_theMagic->mMutex);
        
        char* json = json_dumps(docs, JSON_COMPACT);
        _PomeloRequestResult* rst = new _PomeloRequestResult();
        rst->request = request;
        if(json)
            rst->resp = json;
        rst->status = status;
        gPomelo->_theMagic->pushReqResult(rst);
        free(json);
        
        pthread_mutex_unlock(&gPomelo->_theMagic->mMutex);
    }
}
void CCPomeloImpl::notifyCallback(pc_notify_t *ntf, int status)
{
    if(gPomelo->status() == EPomeloStopping)
    {
        /*
         EPomeloStopping时，表示此函数是由pc_client_destory()内部触发。
         此时处于主线程中。ntf会由libpomelo内部进行释放。
         */
        if(gPomelo->_theMagic->mNtfUserMap.find(ntf) != gPomelo->_theMagic->mNtfUserMap.end())
        {
            _PomeloUser* user = gPomelo->_theMagic->mNtfUserMap[ntf];
#if CCX3
            if(user && user->ntfCB)
            {
                CCPomeloNotifyResult result;
                result.notifyRoute = ntf->route;
                result.status = status;
                
                user->ntfCB(result);
            }
#else
            if(user && user->target && user->ntfSel)
            {
                CCPomeloNotifyResult result;
                result.notifyRoute = ntf->route;
                result.status = status;
                
                PomeloNtfResultHandler sel = user->ntfSel;
                (user->target->*sel)(result);
            }
#endif
            delete user;
            
            //fixme
            json_decref(ntf->msg);
        }
    }
    else    //EPomeloConnected
    {
        pthread_mutex_lock(&gPomelo->_theMagic->mMutex);
        
        _PomeloNotifyResult* rst = new _PomeloNotifyResult();
        rst->notify = ntf;
        rst->status = status;
        gPomelo->_theMagic->pushNtfResult(rst);
        
        pthread_mutex_unlock(&gPomelo->_theMagic->mMutex);
    }
}
void CCPomeloImpl::eventCallback(pc_client_t *client, const char *event, void *data)
{
    pthread_mutex_lock(&gPomelo->_theMagic->mMutex);
    
    if(gPomelo->status() == EPomeloConnected)
    {
        char* json = json_dumps((json_t*)data, JSON_COMPACT);
        _PomeloEvent* rst = new _PomeloEvent();
        rst->event = event;
        if(json)
            rst->data = json;
        gPomelo->_theMagic->pushEvent(rst);
        free(json);
    }
    else    //EPomeloStopping
    {
        //EPomeloStopping过程中不响应callback
    }
    
    pthread_mutex_unlock(&gPomelo->_theMagic->mMutex);
}
void CCPomeloImpl::disconnectedCallback(pc_client_t *client, const char *event, void *data)
{
    _PomeloEvent* rst = new _PomeloEvent();
    rst->event = event;
    gPomelo->_theMagic->pushEvent(rst);
    
    free(data); //data === NULL ?? fixme
}
CCPomeloImpl::~CCPomeloImpl()
{
    //just in case
    stop();
}

CCPomeloImpl::CCPomeloImpl()
:mStatus(EPomeloStopped),
mClient(NULL),
mAsyncConnUser(NULL),
mAsyncConnDispatchPending(false),
mAsyncConn(NULL),
#if CCX3
mDisconnectCB(NULL)
#else
mDisconnectCbTarget(NULL),
mDisconnectCbSelector(NULL)
#endif
{
    //paused by default
#if CCX3
        CCDirector::getInstance()->getScheduler()->scheduleSelector(schedule_selector(CCPomeloImpl::ccDispatcher), this, 0, true);
#else
        CCDirector::sharedDirector()->getScheduler()->scheduleSelector(schedule_selector(CCPomeloImpl::ccDispatcher), this, 0, true);
#endif

    
    pthread_mutex_init(&mMutex, NULL);
}

CCPomeloStatus CCPomeloImpl::status() const
{
    return mStatus;
}

int CCPomeloImpl::connect(const char* host, int port)
{
    if(mStatus == EPomeloStopping)
        return -1;
    
    struct sockaddr_in address;
    memset(&address, 0, sizeof(struct sockaddr_in));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    
    unsigned long vf_addr_ = inet_addr(host);
    if(vf_addr_ == INADDR_NONE)
    {
        struct hostent *pHostEnt= NULL;
        pHostEnt = gethostbyname(host);
        if(pHostEnt != NULL)
        {
            vf_addr_ = *((unsigned long*)pHostEnt->h_addr_list[0]);
        }
    }
    
    address.sin_addr.s_addr = vf_addr_;
    
    //stop any connection
    stop();
    
    mClient = pc_client_new();
    int ret = pc_client_connect(mClient, &address);
    if(ret)
    {
        pc_client_destroy(mClient);
        mClient = NULL;
    }
    else
    {
        mStatus = EPomeloConnected;
        
        pc_add_listener(mClient, PC_EVENT_DISCONNECT, disconnectedCallback);
        
#if CCX3
        CCDirector::getInstance()->getScheduler()->resumeTarget(this);
#else
        CCDirector::sharedDirector()->getScheduler()->resumeTarget(this);
#endif
        
    }
    return ret;
}

#if CCX3
int CCPomeloImpl::connectAsnyc(const char* host, int port, const PomeloAsyncConnCallback& callback)
{
    if(mStatus == EPomeloStopping)
        return -1;
    
    struct sockaddr_in address;
    memset(&address, 0, sizeof(struct sockaddr_in));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    
    unsigned long vf_addr_ = inet_addr(host);
    if(vf_addr_ == INADDR_NONE)
    {
        struct hostent *pHostEnt= NULL;
        pHostEnt = gethostbyname(host);
        if(pHostEnt != NULL)
        {
            vf_addr_ = *((unsigned long*)pHostEnt->h_addr_list[0]);
        }
    }
    
    address.sin_addr.s_addr = vf_addr_;
    
    stop();
    mClient = pc_client_new();
    
    mAsyncConn = pc_connect_req_new(&address);
    int ret = pc_client_connect2(mClient, mAsyncConn, connectAsnycCallback);
    if(ret)
    {
        pc_connect_req_destroy(mAsyncConn);
        pc_client_destroy(mClient);
        mClient = NULL;
        mAsyncConn = NULL;
    }
    else
    {
        mStatus = EPomeloConnecting;
        
        mAsyncConnUser = new _PomeloUser();
        mAsyncConnUser->connCB = callback;
    }
    return ret;
}
int CCPomeloImpl::setDisconnectedCallback(const std::function<void()>& callback)
{
    if(mStatus != EPomeloConnected)
        return -1;
    mDisconnectCB = callback;
    return 0;
}
int CCPomeloImpl::request(const char* route, const std::string& msg, const PomeloReqResultCallback& callback)
{
    if(mStatus != EPomeloConnected)
        return -1;
    
    pc_request_t *req = pc_request_new();
    _PomeloUser* user = new _PomeloUser();
    user->reqCB = callback;
    mReqUserMap[req] = user;    //ownership transferred
    
    json_error_t err;
    json_t* j = json_loads(msg.c_str(), JSON_COMPACT, &err);
    int ret = pc_request(mClient, req, route, j, requestCallback);
    //json_decref(j);
    return ret;
}
int CCPomeloImpl::notify(const char* route, const std::string& msg, const PomeloNtfResultCallback& callback)
{
    if(mStatus != EPomeloConnected)
        return -1;
    
    pc_notify_t *ntf = pc_notify_new();
    _PomeloUser* user = new _PomeloUser();
    user->ntfCB = callback;
    mNtfUserMap[ntf] = user;    //ownership transferred
    
    json_error_t err;
    json_t* j = json_loads(msg.c_str(), JSON_COMPACT, &err);
    int ret = pc_notify(mClient, ntf, route, j, notifyCallback);
    //json_decref(j);
    return ret;
}
int CCPomeloImpl::addListener(const char* event, const PomeloEventCallback& callback)
{
    if(mStatus != EPomeloConnected)
        return -1;
    
    removeListener(event);
    
    int ret = pc_add_listener(mClient, event, eventCallback);
    if(ret == 0)
    {
        _PomeloUser *user = new _PomeloUser();
        user->evtCB = callback;
        
        mEventUserMap[event] = user;
    }
    
    return ret;
}
#else
int CCPomeloImpl::connectAsnyc(const char* host, int port, cocos2d::CCObject* pCallbackTarget, PomeloAsyncConnHandler pCallbackSelector)
{
    struct sockaddr_in address;
    memset(&address, 0, sizeof(struct sockaddr_in));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    
    unsigned long vf_addr_ = inet_addr(host);
    if(vf_addr_ == INADDR_NONE)
    {
        struct hostent *pHostEnt= NULL;
        pHostEnt = gethostbyname(host);
        if(pHostEnt != NULL)
        {
            vf_addr_ = *((unsigned long*)pHostEnt->h_addr_list[0]);
        }
    }
    
    address.sin_addr.s_addr = vf_addr_;
    
    stop();
    mClient = pc_client_new();
    
    mAsyncConn = pc_connect_req_new(&address);
    int ret = pc_client_connect2(mClient, mAsyncConn, connectAsnycCallback);
    if(ret)
    {
        pc_connect_req_destroy(mAsyncConn);
        pc_client_destroy(mClient);
        mClient = NULL;
        mAsyncConn = NULL;
    }
    else
    {
        mStatus = EPomeloConnecting;
        
        mAsyncConnUser = new _PomeloUser();
        mAsyncConnUser->target = pCallbackTarget;
        mAsyncConnUser->connSel = pCallbackSelector;
    }
    return ret;
}

int CCPomeloImpl::request(const char* route, const std::string& msg, cocos2d::CCObject* pCallbackTarget, PomeloReqResultHandler pCallbackSelector)
{
    if(mStatus != EPomeloConnected)
        return -1;
    
    pc_request_t *req = pc_request_new();
    _PomeloUser* user = new _PomeloUser();
    user->target = pCallbackTarget;
    user->reqSel = pCallbackSelector;
    mReqUserMap[req] = user;    //ownership transferred
    
    json_error_t err;
    json_t* j = json_loads(msg.c_str(), JSON_COMPACT, &err);
    int ret = pc_request(mClient, req, route, j, requestCallback);
    //json_decref(j);
    return ret;
}

int CCPomeloImpl::notify(const char* route, const std::string& msg, cocos2d::CCObject* pCallbackTarget, PomeloNtfResultHandler pCallbackSelector)
{
    if(mStatus != EPomeloConnected)
        return -1;
    
    pc_notify_t *ntf = pc_notify_new();
    _PomeloUser* user = new _PomeloUser();
    user->target = pCallbackTarget;
    user->ntfSel = pCallbackSelector;
    mNtfUserMap[ntf] = user;    //ownership transferred
    
    json_error_t err;
    json_t* j = json_loads(msg.c_str(), JSON_COMPACT, &err);
    int ret = pc_notify(mClient, ntf, route, j, notifyCallback);
    //json_decref(j);
    return ret;
}
int CCPomeloImpl::setDisconnectedCallback(cocos2d::CCObject* pTarget, cocos2d::SEL_CallFunc pSelector)
{
    if(mStatus != EPomeloConnected)
        return -1;
    mDisconnectCbTarget = pTarget;
    mDisconnectCbSelector = pSelector;
    return 0;
}
int CCPomeloImpl::addListener(const char* event, cocos2d::CCObject* pCallbackTarget, PomeloEventHandler pCallbackSelector)
{
    if(mStatus != EPomeloConnected)
        return -1;
    
    removeListener(event);
    
    int ret = pc_add_listener(mClient, event, eventCallback);
    if(ret == 0)
    {
        _PomeloUser *user = new _PomeloUser();
        user->target = pCallbackTarget;
        user->evtSel = pCallbackSelector;
        
        mEventUserMap[event] = user;
    }
    
    return ret;
}
#endif

void CCPomeloImpl::removeListener(const char* event)
{
    if(mEventUserMap.find(event) != mEventUserMap.end())
    {
        pc_remove_listener(mClient, event, eventCallback);
        
        delete mEventUserMap[event];
        mEventUserMap.erase(event);
    }
}
void CCPomeloImpl::removeAllListeners()
{
    if(!mEventUserMap.empty())
    {
        map<string,_PomeloUser*>::iterator it;
        for (it = mEventUserMap.begin(); it != mEventUserMap.end(); it++)
        {
            string event = (*it).first;
            pc_remove_listener(mClient, event.c_str(), eventCallback);
            
            delete mEventUserMap[event];
        }
    }
    mEventUserMap.clear();
    
    //drop all pending callback events
    pthread_mutex_lock(&mMutex);
    clearAllPendingEvents();
    pthread_mutex_unlock(&mMutex);
}

void CCPomeloImpl::stop()
{
    pthread_mutex_lock(&mMutex);
    
    switch (mStatus) {
        case EPomeloStopped:
            //do nothing
            break;
        case EPomeloStopping:
            //do nothing
            break;
        case EPomeloConnecting:
        case EPomeloConnected:
        {
            CCPomeloStatus status = mStatus;
            mStatus = EPomeloStopping;  //标记为停止中
            if(status == EPomeloConnecting)
            {
                /*
                 在libpomelo仍处于连接过程中销毁pc_connect_t或pc_client_t，会导致libuv崩溃。
                 */
                //libuv crashes if we destory pc_connect_t or destory
                //pc_client_t when libpomelo is connecting
                //so we simply create a new pc_client_t and ignore
                //the old pc_connect_t
            }
            else    //EPomeloConnected
            {
                pc_remove_listener(mClient, PC_EVENT_DISCONNECT, disconnectedCallback);
                /*
                 注意：pc_client_destroy()内部会触发所有未完成的request/notify的回调。CCPomeloWrapper会将这些回调以【同步方式】扔回给客户端。
                 */
                pc_client_destroy(mClient);
            }
            
            mAsyncConn = NULL;
            mClient = NULL;
            mStatus = EPomeloStopped;   //重置标记
            
            //release resources
            delete mAsyncConnUser;
            mAsyncConnUser = NULL;
            mAsyncConnDispatchPending = false;
            
            clearReqResource();
            clearNtfResource();
            clearAllPendingEvents();
            
#if CCX3
            CCDirector::getInstance()->getScheduler()->pauseTarget(this);
#else
            CCDirector::sharedDirector()->getScheduler()->pauseTarget(this);
#endif
            
            break;
        }
        default:
            break;
    }

    pthread_mutex_unlock(&mMutex);
}

void CCPomeloImpl::clearReqResource()
{
    map<pc_request_t*, _PomeloUser*>::iterator reqIt;
    for (reqIt = mReqUserMap.begin(); reqIt != mReqUserMap.end(); reqIt++)
    {
        pc_request_t* req = (*reqIt).first;
        _PomeloUser* user = (*reqIt).second;
        
        //fixme
        //pc_request_destroy does NOT deal with req->msg
        //so we need to free it manually
        json_decref(req->msg);
        pc_request_destroy(req);
        delete user;
    }
    mReqUserMap.clear();
    
    _PomeloRequestResult* reqRst = NULL;
    while((reqRst = popReqResult(false)))
    {
        delete reqRst;
    }
    queue<_PomeloRequestResult*> empty;
    swap(mReqResultQueue, empty);
}
void CCPomeloImpl::clearNtfResource()
{
    map<pc_notify_t*, _PomeloUser*>::iterator it;
    for (it = mNtfUserMap.begin(); it != mNtfUserMap.end(); it++)
    {
        pc_notify_t* ntf = (*it).first;
        _PomeloUser* user = (*it).second;
        //fixme
        //pc_notify_destroy does NOT deal with ntf->msg
        //so we need to free it manually
        json_decref(ntf->msg);
        pc_notify_destroy(ntf);
        delete user;
    }
    mNtfUserMap.clear();
    
    _PomeloNotifyResult* rst = NULL;
    while((rst = popNtfResult(false)))
    {
        delete rst;
    }
    queue<_PomeloNotifyResult*> empty;
    swap(mNtfResultQueue, empty);
}
void CCPomeloImpl::clearAllPendingEvents()
{
    while (!mEventQueue.empty())
    {
        _PomeloEvent* rst = mEventQueue.front();
        delete rst;
        mEventQueue.pop();
    }
    
    queue<_PomeloEvent*> empty;
    swap(mEventQueue, empty);
}

void CCPomeloImpl::lock()
{
    
}

void CCPomeloImpl::unlock()
{
    
}

//============================================================
CCPomeloWrapper* CCPomeloWrapper::getInstance()
{
    if(!gPomelo)
    {
        gPomelo = new CCPomeloWrapper();
    }
    return gPomelo;
}
CCPomeloWrapper::~CCPomeloWrapper()
{
    delete _theMagic;
}

CCPomeloStatus CCPomeloWrapper::status() const
{
    return _theMagic->status();
}

int CCPomeloWrapper::connect(const char* host, int port)
{
    return _theMagic->connect(host, port);
}

#if CCX3
int CCPomeloWrapper::connectAsnyc(const char* host, int port, const PomeloAsyncConnCallback& callback)
{
    return _theMagic->connectAsnyc(host, port, callback);
}
int CCPomeloWrapper::setDisconnectedCallback(const std::function<void()>& callback)
{
    return _theMagic->setDisconnectedCallback(callback);
}
int CCPomeloWrapper::request(const char* route, const std::string& msg, const PomeloReqResultCallback& callback)
{
    return _theMagic->request(route, msg, callback);
}
int CCPomeloWrapper::notify(const char* route, const std::string& msg, const PomeloNtfResultCallback& callback)
{
    return _theMagic->notify(route, msg, callback);
}
int CCPomeloWrapper::addListener(const char* event, const PomeloEventCallback& callback)
{
    return _theMagic->addListener(event, callback);
}
#else
int CCPomeloWrapper::connectAsnyc(const char* host, int port, cocos2d::CCObject* pCallbackTarget, PomeloAsyncConnHandler pCallbackSelector)
{
    return _theMagic->connectAsnyc(host, port, pCallbackTarget, pCallbackSelector);
}
int CCPomeloWrapper::setDisconnectedCallback(cocos2d::CCObject* pTarget, cocos2d::SEL_CallFunc pSelector)
{
    return _theMagic->setDisconnectedCallback(pTarget, pSelector);
}

int CCPomeloWrapper::request(const char* route, const std::string& msg, cocos2d::CCObject* pCallbackTarget, PomeloReqResultHandler pCallbackSelector)
{
    return _theMagic->request(route, msg, pCallbackTarget, pCallbackSelector);
}

int CCPomeloWrapper::notify(const char* route, const std::string& msg, cocos2d::CCObject* pCallbackTarget, PomeloNtfResultHandler pCallbackSelector)
{
    return _theMagic->notify(route, msg, pCallbackTarget, pCallbackSelector);
}
int CCPomeloWrapper::addListener(const char* event, cocos2d::CCObject* pCallbackTarget, PomeloEventHandler pCallbackSelector)
{
    return _theMagic->addListener(event, pCallbackTarget, pCallbackSelector);
}
#endif



void CCPomeloWrapper::stop()
{
    _theMagic->stop();
}

void CCPomeloWrapper::removeListener(const char* event)
{
    _theMagic->removeListener(event);
}
void CCPomeloWrapper::removeAllListeners()
{
    _theMagic->removeAllListeners();
}
CCPomeloWrapper::CCPomeloWrapper()
{
    _theMagic = new CCPomeloImpl();
}
