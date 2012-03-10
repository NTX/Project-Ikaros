#include "Handler.h"


void SigContHandler(int)
{

}

PacketHandler::PacketHandler():
_diffTime(0), process(true)
{

}

PacketHandler::~PacketHandler()
{
    while (queue.size())
        queue.pop();
    delete sigHandler;
}

void PacketHandler::AddEvent(Event event)
{
    queue.push(event);
}

void PacketHandler::AddDelayedEvent(DelayedEvent event)
{
    delayedQueue.push_back(event);
}

void PacketHandler::UpdateDelayed()
{
    if (!delayedQueue.size())
        return;

    for (DelayedEventQueue::iterator it = delayedQueue.begin(); it != delayedQueue.end(); it++)
    {
        it->Update(getMsTimeDiffToNow(it->updated));
        if (it->Execute())
            queue.push(*it);
    }

    for (DelayedEventQueue::iterator it = delayedQueue.begin(); it != delayedQueue.end(); it++)
        if (it->Execute())
        {
            delayedQueue.erase(it);
            it = delayedQueue.begin();
            if (!delayedQueue.size())
                break;
        }
}

void PacketHandler::Terminate()
{
    sLog->outControl();
    sLog->outControl("[PacketHandler] Droping %d delayed events.", delayedQueue.size());
    delayedQueue.clear();
    sLog->outControl("[PacketHandler] Droping %d immediate events.", queue.size());
    while (queue.size())
        queue.pop();
    process = false;
}

void PacketHandler::ProcessQueue()
{
    assert(app);
    sigHandler = new SignalHandler();
    sigHandler->setSignalHandler(Continue, SigContHandler);
    sLog->outControl("[PacketHandler] Process Queue initialization");
    sigset_t suspendSig;
    sigemptyset(&suspendSig);
    int sig = SIGCONT;

    timeval workBegan;
    gettimeofday(&workBegan, 0);

    while (process)
    {
        UpdateDelayed();
        _diffTime = getMsTimeDiffToNow(workBegan);
        gettimeofday(&workBegan, 0);
        if (!queue.size() && !delayedQueue.size())
        {
            //sLog->outDebug("Process Queue goes to sleep (until data arrives)");
            sigaddset(&suspendSig, SIGCONT);
            sigaddset(&suspendSig, SIGINT);
            sigaddset(&suspendSig, SIGTERM);
            app->threadMgr->SetThreadStatus(GetThisThread(), THREAD_SUSPENDED);
            if (app->freezeDetector)
                app->freezeDetector->Pause();
            sigwait(&suspendSig, &sig);
            app->threadMgr->SetThreadStatus(GetThisThread(), THREAD_ACTIVE);
            if (app->freezeDetector)
                app->freezeDetector->Continue();
            //sLog->outDebug("Process Queue was suspended %lu milliseconds", getMsTimeDiffToNow(workBegan));
        }

        if (queue.size())
        {
            Event& event = queue.front();
            switch (event.EventType)
            {
                case EVENT_CONNECT:
                    if (app->proto.OnConnect)
                        app->proto.OnConnect(event.address, event.fd);
                    break;
                case EVENT_RECIEVE:
                    if (app->proto.OnRecieve)
                        app->proto.OnRecieve(event.address, event.fd, event.data);
                    delete &event.data;
                    break;
                case EVENT_SEND:
                break;
                case EVENT_DISCONNECT:
                    if (app->proto.OnDisconnect)
                        app->proto.OnDisconnect(event.address, event.fd);
                break;
            }
            //sLog->outDebug("Process Queue Update after diff: %lu milliseconds", getMsTimeDiffToNow(workBegan));
            queue.pop();
        }
        else
            if (delayedQueue.size())
                msleep(500);
    }
    app->freezeDetector->Exit();
    sLog->outControl("[PacketHandler] Process Queue exit");
    ThreadMgr::Exit(NULL);
}
