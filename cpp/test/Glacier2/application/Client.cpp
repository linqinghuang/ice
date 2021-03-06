// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#include <IceUtil/IceUtil.h>
#include <Ice/Ice.h>
#include <Glacier2/Glacier2.h>

#include <TestCommon.h>

#include <iostream>
#include <iomanip>
#include <list>

#include <Callback.h>

using namespace std;
using namespace Test;

namespace
{

class CallbackReceiverI : public Test::CallbackReceiver
{
public:

    CallbackReceiverI() : _received(false)
    {
    }

    virtual void callback(const Ice::Current&)
    {
        IceUtil::Monitor<IceUtil::Mutex>::Lock lock(_monitor);
        _received = true;
        _monitor.notify();
    }

    void waitForCallback()
    {
        IceUtil::Monitor<IceUtil::Mutex>::Lock lock(_monitor);
        while(!_received)
        {
            _monitor.wait();
        }
        _received = false;
    }

    IceUtil::Monitor<IceUtil::Mutex> _monitor;
    bool _received;
};
ICE_DEFINE_PTR(CallbackReceiverIPtr, CallbackReceiverI);

class Application : public Glacier2::Application
{
public:

    Application() : _restart(0), _destroyed(false), _receiver(ICE_MAKE_SHARED(CallbackReceiverI))
    {
    }

    virtual Glacier2::SessionPrxPtr
    createSession()
    {
        return ICE_UNCHECKED_CAST(Glacier2::SessionPrx, router()->createSession("userid", "abc123"));
    }

    virtual int
    runWithSession(int, char*[])
    {
        test(router());
        test(!categoryForClient().empty());
        test(objectAdapter());

        if(_restart == 0)
        {
            cout << "testing Glacier2::Application restart... " << flush;
        }
        Ice::ObjectPrxPtr base = communicator()->stringToProxy("callback:" + getTestEndpoint(communicator(), 0));
        CallbackPrxPtr callback = ICE_UNCHECKED_CAST(CallbackPrx, base);
        if(++_restart < 5)
        {
            CallbackReceiverPrxPtr receiver = ICE_UNCHECKED_CAST(CallbackReceiverPrx, addWithUUID(_receiver));
            callback->initiateCallback(receiver);
            _receiver->waitForCallback();
            restart();
        }
        cout << "ok" << endl;

        cout << "testing server shutdown... " << flush;
        callback->shutdown();
        cout << "ok" << endl;

        return 0;
    }

    virtual void sessionDestroyed()
    {
        _destroyed = true;
    }

    int _restart;
    bool _destroyed;
    CallbackReceiverIPtr _receiver;
};

} // anonymous namespace end

int
main(int argc, char* argv[])
{
#ifdef ICE_STATIC_LIBS
    Ice::registerIceSSL(false);
    Ice::registerIceWS(true);
#endif
    Application app;
    Ice::InitializationData initData = getTestInitData(argc, argv);
    initData.properties->setProperty("Ice.Warn.Connections", "0");
    initData.properties->setProperty("Ice.Default.Router", "Glacier2/router:" + getTestEndpoint(initData.properties, 10));
    int status = app.main(argc, argv, initData);

    initData.properties->setProperty("Ice.Default.Router", "");
    Ice::CommunicatorPtr communicator = Ice::initialize(initData);

    cout << "testing stringToProxy for process object... " << flush;
    Ice::ObjectPrxPtr processBase = communicator->stringToProxy("Glacier2/admin -f Process:" + getTestEndpoint(communicator, 11));
    cout << "ok" << endl;

    cout << "testing checked cast for admin object... " << flush;
    Ice::ProcessPrxPtr process = ICE_CHECKED_CAST(Ice::ProcessPrx, processBase);
    test(process != 0);
    cout << "ok" << endl;

    cout << "testing Glacier2 shutdown... " << flush;
    process->shutdown();
    try
    {
        process->ice_ping();
        test(false);
    }
    catch(const Ice::LocalException&)
    {
        cout << "ok" << endl;
    }

    test(app._restart == 5);
    test(app._destroyed);

    communicator->destroy();
    return status;
}
