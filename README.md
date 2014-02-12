CCPomeloWrapper
===============

A simple libpomelo wrapper for cocos2d-x

Version: 0.9 2014/02/12

Great thanks to https://github.com/xdxttt/CCPomelo

如何使用
===============
0. 配置好你的cocos2d-x+libpomelo工程，可以参考http://laoyur.ml/?p=318
1. 下载CCPomeloWrapper.cpp/h到你的项目中
2. 使用以下示例代码和chatofpomelo-websocket服务端通信

示例代码for cocos2dx 3.0（2.x的示例代码请参考下文中的英文示例）
===============
```
    //连接gate
    CCPomeloWrapper* pomelo = CCPomeloWrapper::getInstance();
    pomelo->connectAsnyc("yourserver.com", 3014, [=](int err){
        if(err == 0)
        {
            //gate连接成功
            
            //向gate查询connector信息
            const char *route = "gate.gateHandler.queryEntry";
            Json::Value msg;  //you need to include "jsoncpp.h"
            Json::FastWriter writer;
            
            msg["uid"] = "111"; //your uid
            
            pomelo->request(route, writer.write(msg), [=](const CCPomeloRequestResult& result){
                
                Json::Value root;
                Json::Reader reader;
                if(reader.parse(result.jsonMsg, root))
                {
                    if(root["code"].asInt() == 200)
                    {
                        string host = root["host"].asString();
                        int port = root["port"].asInt();
                        
                        //断开与gate的连接
                        pomelo->stop();
                        
                        //连接到connector
                        pomelo->connectAsnyc(host.c_str(), port, [=](int err2){
                            
                            if(err2 == 0)
                            {
                                //connector连接成功
                                
                                //订阅掉线事件
                                pomelo->setDisconnectedCallback([]{
                                    //
                                    CCLOG("connection lost");
                                });
                                
                                //进入房间
                                Json::Value msg;
                                Json::FastWriter writer;
                                
                                msg["rid"] = "1"; //room id
                                msg["username"] = "cocos2dx-client-1";
                                
                                pomelo->request("connector.entryHandler.enter", writer.write(msg), [=](const CCPomeloRequestResult& result){
                                    
                                    //监听聊天消息
                                    pomelo->addListener("onChat", [=](const CCPomeloEvent& event){
                                        
                                        //收到聊天消息
                                        CCLOG("%s: %s", event.event.c_str(), event.jsonMsg.c_str());
                                        
                                    });
                                    
                                    //测试向所有人发送消息
                                    Json::Value msg;
                                    Json::FastWriter writer;
                                    
                                    msg["content"] = "hello CCPomelo";
                                    msg["target"] = "*";  //send to all
                                    
                                    pomelo->request("chat.chatHandler.send", writer.write(msg), [=](const CCPomeloRequestResult& sendResult){
                                        
                                        //CCLOG("msg sent");
                                        
                                    });
                                    
                                });
                            }
                            else
                            {
                                CCLOG("connect to connector failed with code: %d", err2);
                            }
                        });
                    }
                }
                
            });
        }
        else
        {
            CCLOG("connect to gate failed with code: %d", err);
        }
    });
```


How to use

0. setup your cocos2d-x project with libpomelo supported first
1. add CCPomeloWrapper.cpp/h to your project
2. sample code for connecting with chatofpomelo-websocket:


```

//connect to gate:
void SampleScene::testMenuClicked(cocos2d::CCObject* pSender)
{
    //***DO NOT use my sample server directly. It is not available anymore***
    if(CCPomeloWrapper::getInstance()->connect("dev1.laoyur.ml", 3014)) 
    {
        CCLOG("connect failed ");
    }
    else
    {
        const char *route = "gate.gateHandler.queryEntry";
        Json::Value msg;  //you need to include "jsoncpp.h"
        Json::FastWriter writer;
        
        msg["uid"] = "1"; //your uid
        
        CCPomeloWrapper::getInstance()->request(route, writer.write(msg), this,  pomelo_req_result_cb_selector(SampleScene::queryEntryCB));
    }
}

void SampleScene::queryEntryCB(const CCPomeloRequestResult& result)
{
    CCLOG("queryEntry %s", result.jsonMsg.c_str());
    
    Json::Value root;
    Json::Reader reader;
    if(reader.parse(result.jsonMsg, root))
    {
        if(root["code"].asInt() == 200)
        {
            string host = root["host"].asString();
            int port = root["port"].asInt();
            
            //stop gate connection
            CCPomeloWrapper::getInstance()->stop();
            
            //connect to connector
            //we use async connect version here
            CCPomeloWrapper::getInstance()->connectAsnyc(host.c_str(), port, this, pomelo_async_conn_cb_selector(SampleScene::connectCB));
            
            //FYI: you can call stop() when async-conn is in progress
            CCPomeloWrapper::getInstance()->stop();
            
            //actually connect to the connector
            CCPomeloWrapper::getInstance()->connectAsnyc(host.c_str(), port, this, pomelo_async_conn_cb_selector(SampleScene::connectCB));
            
            //sync connect version
            /*
            if(CCPomeloWrapper::getInstance()->connect(host.c_str(), port) == 0)
            {
                Json::Value msg;
                Json::FastWriter writer;
                
                msg["rid"] = "1";
                msg["username"] = "user1";
                
                CCPomeloWrapper::getInstance()->request("connector.entryHandler.enter", writer.write(msg), this, pomelo_req_result_cb_selector(SampleScene::entryCB));
            }
            else
            {
                //error
            }
             */
        }
    }
}

void SampleScene::connectCB(int result)
{
    //now we need to listen to 'disconnect' event
    CCPomeloWrapper::getInstance()->setDisconnectedCallback(this, callfunc_selector(SampleScene::onConnLost));
    
    Json::Value msg;
    Json::FastWriter writer;
    
    msg["rid"] = "1"; //room id
    msg["username"] = "user1";
    
    CCPomeloWrapper::getInstance()->request("connector.entryHandler.enter", writer.write(msg), this, pomelo_req_result_cb_selector(SampleScene::entryCB));
}

void SampleScene::entryCB(const CCPomeloRequestResult& result)
{
    CCLOG("entryCB %s", result.jsonMsg.c_str());
    
    //listen to chat event
    CCPomeloWrapper::getInstance()->addListener("onChat",this,  pomelo_listener_cb_selector(SampleScene::onChat));
    
    //we simply send message to all to test
    Json::Value msg;
    Json::FastWriter writer;
    
    msg["content"] = "hello CCPomelo";
    msg["target"] = "*";  //send to all
    
    const char *route = "chat.chatHandler.send";

    CCPomeloWrapper::getInstance()->request(route, writer.write(msg), this,  pomelo_req_result_cb_selector(SampleScene::sendCB));
}

void SampleScene::sendCB(const CCPomeloRequestResult& result)
{
    CCLOG("%s: %s", result.requestRoute.c_str(), result.jsonMsg.c_str());
}

void SampleScene::onChat(const CCPomeloEvent& event)
{
    CCLOG("%s: %s", event.event.c_str(), event.jsonMsg.c_str());
}

void SampleScene::onConnLost()
{
    CCLOG("connection lost");
}

```
TODO:
===============

support async dns resolving for async-connection

support multiple listeners to the same event
