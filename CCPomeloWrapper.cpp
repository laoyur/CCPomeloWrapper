//
//  CCPomeloWrapper.cpp
//  SGXXZ
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
    CCObject* target;   //by ref
    union
    {
        PomeloAsyncConnHandler connSel; //for async conn
        PomeloReqResultHandler reqSel;  //for request
        PomeloNtfResultHandler ntfSel;  //for notify
        PomeloEventHandler evtSel;      //for listener
    };
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
class CCPomeloImpl : public cocos2d::CCObject
{
   
public:
    CCPomeloImpl();
    virtual ~CCPomeloImpl();
    
    CCPomeloStatus status() const;
    
    int connect(const char* host, int port);
    
    int connectAsnyc(const char* host, int port, cocos2d::CCObject* pCallbackTarget, PomeloAsyncConnHandler pCallbackSelector);
    
    void stop();
    
    int request(const char* route, const std::string& msg, cocos2d::CCObject* pCallbackTarget, PomeloReqResultHandler pCallbackSelector);
    
    int notify(const char* route, const std::string& msg, cocos2d::CCObject* pCallbackTarget, PomeloNtfResultHandler pCallbackSelector);
    
    int listen(const char* event, cocos2d::CCObject* pCallbackTarget, PomeloEventHandler pCallbackSelector);
    
private:
    //callbacks for libpomelo
    static void connectAsnycCallback(pc_connect_t* conn_req, int status);
    static void requestCallback(pc_request_t *request, int status, json_t *docs);
    static void notifyCallback(pc_notify_t *ntf, int status);
    static void eventCallback(pc_client_t *client, const char *event, void *data);
    
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
    void clearEventResource();
    
private:
    CCPomeloStatus      mStatus;
    pc_client_t*        mClient;
    
    _PomeloUser*            mAsyncConnUser;
    bool                    mAsyncConnDispatchPending;
    int                     mAsyncConnStatus;
    
    map<pc_request_t*,_PomeloUser*> mReqMap;
    pthread_mutex_t  mReqResultQueueMutex;
    queue<_PomeloRequestResult*> mReqResultQueue;
    
    map<string,_PomeloUser*> mEventMap;
    pthread_mutex_t  mEventQueueMutex;
    queue<_PomeloEvent*> mEventQueue;
    
    map<pc_notify_t*,_PomeloUser*> mNtfMap;
    pthread_mutex_t  mNtfResultQueueMutex;
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
        
        _PomeloUser* user = mAsyncConnUser;
        if(user && user->target && user->connSel)
        {
            PomeloAsyncConnHandler sel = user->connSel;
            (user->target->*sel)(mAsyncConnStatus);
        }
        
        if(mAsyncConnUser == user)  //just in case of stop() called in cb
        {
            delete mAsyncConnUser;
            mAsyncConnUser = NULL;
        }
    }
}
void CCPomeloImpl::dispatchRequestCallbacks()
{
    _PomeloRequestResult* rst = popReqResult();
    if(rst)
    {
        _PomeloUser* user = NULL;
        
        if(mReqMap.find(rst->request) != mReqMap.end())
        {
            user = mReqMap[rst->request];
            mReqMap.erase(rst->request);
        }
        
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
        delete user;
        delete rst;
    }
}
void CCPomeloImpl::dispatchNotifyCallbacks()
{
    _PomeloNotifyResult* rst = popNtfResult();
    if(rst)
    {
        _PomeloUser* user = NULL;
        
        if(mNtfMap.find(rst->notify) != mNtfMap.end())
        {
            user = mNtfMap[rst->notify];
            mNtfMap.erase(rst->notify);
        }
        
        if(user && user->target && user->ntfSel)
        {
            CCPomeloNotifyResult result;
            result.notifyRoute = rst->notify->route;
            result.status = rst->status;

            PomeloNtfResultHandler sel = user->ntfSel;
            (user->target->*sel)(result);
        }
        delete user;
        delete rst;
    }
}
void CCPomeloImpl::dispatchEventCallbacks()
{
    _PomeloEvent* rst = popEvent();
    if(rst)
    {
        _PomeloUser* user = NULL;
        
        if(mEventMap.find(rst->event) != mEventMap.end())
        {
            user = mEventMap[rst->event];
            mEventMap.erase(rst->event);
        }
        
        if(user && user->target && user->evtSel)
        {
            CCPomeloEvent result;
            result.event = rst->event;
            result.jsonMsg = rst->data;

            PomeloEventHandler sel = user->evtSel;
            (user->target->*sel)(result);
        }
        delete user;
        delete rst;
    }
}

void CCPomeloImpl::pushReqResult(_PomeloRequestResult* reqResult)
{
    pthread_mutex_lock(&mReqResultQueueMutex);
    mReqResultQueue.push(reqResult);
    pthread_mutex_unlock(&mReqResultQueueMutex);
}
void CCPomeloImpl::pushNtfResult(_PomeloNotifyResult* ntfResult)
{
    pthread_mutex_lock(&mNtfResultQueueMutex);
    mNtfResultQueue.push(ntfResult);
    pthread_mutex_unlock(&mNtfResultQueueMutex);
}
void CCPomeloImpl::pushEvent(_PomeloEvent* event)
{
    pthread_mutex_lock(&mEventQueueMutex);
    mEventQueue.push(event);
    pthread_mutex_unlock(&mEventQueueMutex);
}

_PomeloRequestResult* CCPomeloImpl::popReqResult(bool lock /*= true*/)
{
    if (mReqResultQueue.size() > 0)
    {
        if(lock)
            pthread_mutex_lock(&mReqResultQueueMutex);
        _PomeloRequestResult *rst = mReqResultQueue.front();
        mReqResultQueue.pop();
        if(lock)
            pthread_mutex_unlock(&mReqResultQueueMutex);
        return rst;
    }
    else
    {
        return  NULL;
    }
}
_PomeloNotifyResult* CCPomeloImpl::popNtfResult(bool lock /*= true*/)
{
    if (mNtfResultQueue.size() > 0)
    {
        if(lock)
            pthread_mutex_lock(&mNtfResultQueueMutex);
        _PomeloNotifyResult *rst = mNtfResultQueue.front();
        mNtfResultQueue.pop();
        if(lock)
            pthread_mutex_unlock(&mNtfResultQueueMutex);
        return rst;
    }
    else
    {
        return  NULL;
    }
}
_PomeloEvent* CCPomeloImpl::popEvent(bool lock /*= true*/)
{
    if (mEventQueue.size() > 0)
    {
        if(lock)
            pthread_mutex_lock(&mEventQueueMutex);
        _PomeloEvent *rst = mEventQueue.front();
        mEventQueue.pop();
        if(lock)
            pthread_mutex_unlock(&mEventQueueMutex);
        return rst;
    }
    else
    {
        return  NULL;
    }
}

void CCPomeloImpl::connectAsnycCallback(pc_connect_t* conn_req, int status)
{
    pc_connect_req_destroy(conn_req);
    
    if(gPomelo->status() == EPomeloConnecting)
    {
        gPomelo->_theMagic->mAsyncConnStatus = status;
        gPomelo->_theMagic->mAsyncConnDispatchPending = true;
    }
}

void CCPomeloImpl::requestCallback(pc_request_t *request, int status, json_t *docs)
{
    char* json = json_dumps(docs, JSON_COMPACT);
    _PomeloRequestResult* rst = new _PomeloRequestResult();
    rst->request = request;
    if(json)
        rst->resp = json;
    rst->status = status;
    gPomelo->_theMagic->pushReqResult(rst);
    free(json);
}
void CCPomeloImpl::notifyCallback(pc_notify_t *ntf, int status)
{
    _PomeloNotifyResult* rst = new _PomeloNotifyResult();
    rst->notify = ntf;
    rst->status = status;
    gPomelo->_theMagic->pushNtfResult(rst);
}
void CCPomeloImpl::eventCallback(pc_client_t *client, const char *event, void *data)
{
    char* json = json_dumps((json_t*)data, JSON_COMPACT);
    _PomeloEvent* rst = new _PomeloEvent();
    rst->event = event;
    if(json)
        rst->data = json;
    gPomelo->_theMagic->pushEvent(rst);
    free(json);
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
mAsyncConnDispatchPending(false)
{
    //paused by default
    CCDirector::sharedDirector()->getScheduler()->scheduleSelector(schedule_selector(CCPomeloImpl::ccDispatcher), this, 0, true);
    
    
    pthread_mutex_init(&mReqResultQueueMutex, NULL);
    pthread_mutex_init(&mNtfResultQueueMutex, NULL);
    pthread_mutex_init(&mEventQueueMutex, NULL);
}

CCPomeloStatus CCPomeloImpl::status() const
{
    return mStatus;
}

int CCPomeloImpl::connect(const char* host, int port)
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
        
        CCDirector::sharedDirector()->getScheduler()->resumeTarget(this);
    }
    return ret;
}

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
    
    pc_connect_t* async = pc_connect_req_new(&address);
    int ret = pc_client_connect2(mClient, async, connectAsnycCallback);
    if(ret)
    {
        pc_client_destroy(mClient);
        mClient = NULL;
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
    mReqMap[req] = user;    //ownership transferred
    
    json_error_t err;
    json_t* j = json_loads(msg.c_str(), JSON_COMPACT, &err);
    return pc_request(mClient, req, route, j, requestCallback);
}

int CCPomeloImpl::notify(const char* route, const std::string& msg, cocos2d::CCObject* pCallbackTarget, PomeloNtfResultHandler pCallbackSelector)
{
    if(mStatus != EPomeloConnected)
        return -1;
    
    pc_notify_t *ntf = pc_notify_new();
    _PomeloUser* user = new _PomeloUser();
    user->target = pCallbackTarget;
    user->ntfSel = pCallbackSelector;
    mNtfMap[ntf] = user;    //ownership transferred
    
    json_error_t err;
    json_t* j = json_loads(msg.c_str(), JSON_COMPACT, &err);

    return pc_notify(mClient, ntf, route, j, notifyCallback);
}

int CCPomeloImpl::listen(const char* event, cocos2d::CCObject* pCallbackTarget, PomeloEventHandler pCallbackSelector)
{
    if(mStatus != EPomeloConnected)
        return -1;

    if(mEventMap.find(event) != mEventMap.end())
    {
        //the event already in listening
        delete mEventMap[event];
    }
    
    _PomeloUser *user = new _PomeloUser();
    user->target = pCallbackTarget;
    user->evtSel = pCallbackSelector;

    mEventMap[event] = user;
    return pc_add_listener(mClient, event, eventCallback);
}

void CCPomeloImpl::stop()
{
    if(mClient)
    {
        //you should not call pc_client_stop() in main thread
        //https://github.com/NetEase/pomelo/issues/208
        
        pc_client_destroy(mClient);
        mClient = NULL;
    }

    mStatus = EPomeloStopped;

    delete mAsyncConnUser;
    mAsyncConnUser = NULL;
    mAsyncConnDispatchPending = false;
    
    clearReqResource();
    clearNtfResource();
    clearEventResource();
    
    CCDirector::sharedDirector()->getScheduler()->pauseTarget(this);
}

void CCPomeloImpl::clearReqResource()
{
    map<pc_request_t*, _PomeloUser*>::iterator reqIt;
    for (reqIt = mReqMap.begin(); reqIt != mReqMap.end(); reqIt++)
    {
        pc_request_t* req = (*reqIt).first;
        _PomeloUser* user = (*reqIt).second;
        pc_request_destroy(req);
        delete user;
    }
    mReqMap.clear();
    
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
    for (it = mNtfMap.begin(); it != mNtfMap.end(); it++)
    {
        pc_notify_t* ntf = (*it).first;
        _PomeloUser* user = (*it).second;
        pc_notify_destroy(ntf);
        delete user;
    }
    mNtfMap.clear();
    
    _PomeloNotifyResult* rst = NULL;
    while((rst = popNtfResult(false)))
    {
        delete rst;
    }
    queue<_PomeloNotifyResult*> empty;
    swap(mNtfResultQueue, empty);
}
void CCPomeloImpl::clearEventResource()
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

int CCPomeloWrapper::connectAsnyc(const char* host, int port, cocos2d::CCObject* pCallbackTarget, PomeloAsyncConnHandler pCallbackSelector)
{
    return _theMagic->connectAsnyc(host, port, pCallbackTarget, pCallbackSelector);
}

void CCPomeloWrapper::stop()
{
    _theMagic->stop();
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
    return _theMagic->listen(event, pCallbackTarget, pCallbackSelector);
}

CCPomeloWrapper::CCPomeloWrapper()
{
    _theMagic = new CCPomeloImpl();
}
