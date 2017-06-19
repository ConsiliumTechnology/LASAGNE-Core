/***************************************************************
    Copyright 2016, 2017 Defence Science and Technology Group,
    Department of Defence,
    Australian Government

	This file is part of LASAGNE.

    LASAGNE is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as
    published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version.

    LASAGNE is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with LASAGNE.  If not, see <http://www.gnu.org/licenses/>.
***************************************************************/
#define DAF_TASKEXECUTOR_CPP

#include "TaskExecutor.h"

#include "Exception.h"
#include "ShutdownHandler.h"
#include "PropertyManager.h"

#if defined(ACE_WIN32)
# include "SYNCHConditionBase.h"
#endif

#include <ace/Singleton.h>

#include <limits>
#include <typeinfo>

#if defined (ACE_HAS_SIG_C_FUNC)
extern "C" void DAF_TaskExecutor_threadCleanup(void *obj, void *args)
{
    DAF::TaskExecutor::threadCleanup(obj, args);
}
#endif /* ACE_HAS_SIG_C_FUNC */

namespace DAF
{
    namespace { // Anonymous

        int make_grp_id(void *p)
        {
            return int(reinterpret_cast<size_t>(p) & size_t(std::numeric_limits<int>::max()));
        }

    } // Anonymous

    template <> inline Runnable_ref
    SynchronousChannel<Runnable_ref>::extract(void)
    {
        ACE_GUARD_REACTION(ACE_SYNCH_MUTEX, guard, *this, throw LockFailureException());
        Runnable_ref t(this->item_._retn()); this->itemTaken_.release();
        if (this->itemError_) {
            this->itemError_ = false; throw InternalException();
        }
        return t._retn();
    }

    /*********************************************************************************/

    ACE_THR_FUNC_RETURN
    TaskExecutor::threadExecute(void * args)
    {
        DAF_OS::thread_0_SIGSET_T(); // Ignore all signals to avoid ERROR:

        WorkerTask_ref worker(reinterpret_cast<WorkerTask_ptr>(args));

        // This is exactly the same logics as ACE_Task_Base::svc_run however it works through a svc proxy

        if (worker) for (TaskExecutor * task(worker->taskExecutor()); task;) {

            ACE_Thread_Descriptor * td = static_cast<Thread_Manager *>(task->thr_mgr())->thread_desc_self();

            // Register ourself with our <Thread_Manager>'s thread exit hook
            // mechanism so that our close() hook will be sure to get invoked
            // when this thread exits.

#if defined(ACE_HAS_SIG_C_FUNC)
            td->at_exit(task, DAF_TaskExecutor_threadCleanup, td);
#else
            td->at_exit(task, DAF::TaskExecutor::threadCleanup, td);
#endif /* ACE_HAS_SIG_C_FUNC */

            ACE_THR_FUNC_RETURN status = 0;

            try {  // Stop Application code from killing Server

                // Call the Task's run() hook method.
#if defined(ACE_HAS_INTEGRAL_TYPE_THR_FUNC_RETURN)
                status = static_cast<ACE_THR_FUNC_RETURN>(worker->run());
#else
                status = reinterpret_cast<ACE_THR_FUNC_RETURN>(worker->run());
#endif /* ACE_HAS_INTEGRAL_TYPE_THR_FUNC_RETURN */

            } DAF_CATCH_ALL {
                if (ACE::debug() || DAF::debug()) {
                    ACE_DEBUG((LM_ERROR, ACE_TEXT("DAF (%P | %t) TaskExecutor ERROR: ")
                        ACE_TEXT("Unhandled exception caught from execute_run() : TaskExecutor=0x%@.\n"), task));
                }
            }

            static_cast<Thread_Descriptor *>(td)->threadAtExit(false);

            return status;
        }

        return ACE_THR_FUNC_RETURN(-1);
    }

    void
    TaskExecutor::threadCleanup(void * obj, void * args)
    {
        if (obj) {

            TaskExecutor * task(reinterpret_cast<TaskExecutor *>(obj));

            if (task) do {

                ACE_thread_t thr_mine = DAF_OS::thr_self(), thr_self(0);

                {
                    ACE_GUARD_REACTION(ACE_Thread_Mutex, ace_mon, task->lock_, break);

                    if (args) {

                        Thread_Descriptor * td = reinterpret_cast<Thread_Descriptor *>(args);

                        if (td) {
                            thr_self = td->self();
                            if (DAF::debug() > 2) {
                                ACE_DEBUG((LM_INFO, ACE_TEXT("DAF (%P | %t) TaskExecutor::cleanup; ")
                                    ACE_TEXT("grp_id=%d,thr_count=%d,thr_self=%d[0x%X],ThreadState=0x%X\n")
                                    , task->grp_id()
                                    , task->thr_count()
                                    , unsigned(thr_self), unsigned(thr_self)
                                    , unsigned(td->threadState())));
                            }
                        }

                    }

                    if (0 == --task->thr_count_) {
                        task->last_thread_id_ = (thr_self ? thr_self : thr_mine);
                    }

                    task->zeroCondition_.signal(); // Signal thread is leaving (only 1 waiter)
                }

                if (thr_mine == thr_self) { // Only call close here if we ARE the closing thread
                    task->close(0); // *task* is undefined here. close() could have deleted it.
                }

            } while (false);
        }
    }

    /*********************************************************************************/

    TaskExecutor::TaskExecutor(void) : ACE_Task_Base(new Thread_Manager())
        , threadManager_    (this->thr_mgr()) // Contains lifecycle of local ACE_Thread_Manager
        , zeroCondition_    (this->lock_)
        , decay_timeout_    (THREAD_DECAY_TIMEOUT)
        , evict_timeout_    (THREAD_EVICT_TIMEOUT)
        , handoff_timeout_  (THREAD_HANDOFF_TIMEOUT)
        , executorAvailable_(true)
        , executorClosed_   (false)
    {
        this->grp_id(make_grp_id(this));

        time_t decay_timeout(DAF::get_numeric_property<time_t>(DAF_TASKDECAYTIMEOUT, THREAD_DECAY_TIMEOUT / DAF_MSECS_ONE_SECOND, true));
        this->setDecayTimeout(decay_timeout * DAF_MSECS_ONE_SECOND);
        time_t evict_timeout(DAF::get_numeric_property<time_t>(DAF_TASKEVICTTIMEOUT, THREAD_EVICT_TIMEOUT / DAF_MSECS_ONE_SECOND, true));
        this->setEvictTimeout(evict_timeout * DAF_MSECS_ONE_SECOND);
        time_t handoff_timeout(DAF::get_numeric_property<time_t>(DAF_TASKHANDOFFTIMEOUT, time_t(THREAD_HANDOFF_TIMEOUT), true));
        this->setHandoffTimeout(handoff_timeout);
    }

    TaskExecutor::~TaskExecutor(void)
    {
        this->module_closed(); this->wait(); this->thr_mgr(0); // Close-and-Wait for threads to exit cleanly
    }

    size_t
    TaskExecutor::thr_count(void) const
    {
        return this->thr_count_;
    }

    int
    TaskExecutor::svc(void)
    {
        if (DAF::debug() > 1) {
            ACE_DEBUG((LM_WARNING,
                ACE_TEXT("DAF (%P | %t) WARNING: TaskExecutor; Activated with nothing Running.\n")));
        }
        return 0; // Do Nothing
    }

    int
    TaskExecutor::svc(const Runnable_ref & command)
    {
        return (DAF::is_nil(command) ? 0 : command->run()); // Run the Command
    }

    bool
    TaskExecutor::isAvailable(void) const
    {
        if (this->executorAvailable_) {
            if (DAF::ShutdownHandler::has_shutdown()) {
                this->executorAvailable_ = false;
            } else {
                return true;
            }
        }
        return false;
    }

    void
    TaskExecutor::setDecayTimeout(time_t timeout_milliseconds) // milliseconds
    {
        this->decay_timeout_ = ace_range(DAF_MSECS_ONE_SECOND, DAF_MSECS_ONE_HOUR, timeout_milliseconds);
    }

    void
    TaskExecutor::setEvictTimeout(time_t timeout_milliseconds) // milliseconds
    {
        this->evict_timeout_ = ace_range(DAF_MSECS_ONE_SECOND, DAF_MSECS_ONE_MINUTE, timeout_milliseconds);
    }

    void
    TaskExecutor::setHandoffTimeout(time_t timeout_milliseconds) // milliseconds
    {
        this->handoff_timeout_ = ace_range(time_t(0), time_t(10), timeout_milliseconds);
    }

    int
    TaskExecutor::execute(size_t n_threads,
        bool            force_active,
        long            flags,
        long            priority,
        ACE_hthread_t   thread_handles[],
        void *          stack[],
        size_t          stack_size[],
        ACE_thread_t    thread_ids[],
        const char *    thr_name[])
    {
        if (ACE_BIT_ENABLED(flags, (THR_DETACHED | THR_DAEMON))) {
            ACE_DEBUG((LM_WARNING, ACE_TEXT("(%P | %t) DAF::TaskExecutor::execute; ")
                ACE_TEXT("Unsupported flags THR_DETACHED and THR_DAEMON - flags reset.\n")));
            ACE_CLR_BITS(flags, (THR_DETACHED | THR_DAEMON));
        }

        ACE_SET_BITS(flags, (THR_NEW_LWP | THR_JOINABLE)); // Ensure all threads in pool have consistant attributes

        if (n_threads && this->isAvailable()) do {

            WorkerTask_ref tp(new WorkerTask(this));

            { // Scope Lock

                ACE_GUARD_REACTION(ACE_Thread_Mutex, mon, this->lock_, break);

                if (this->isAvailable() ? (force_active ? false : this->thr_count()) : true) {  // DCL
                    break; // Not available OR already active without being forced
                }

                for (size_t thr_count = n_threads; thr_count--;) {
                    ++this->thr_count_; WorkerTask::intrusive_add_ref(tp);  // Increment refcount by n_threads
                }

                int grp_spawned = -1;

                if (thread_ids) {
                    // thread names were specified
                    grp_spawned = this->thr_mgr_->spawn_n(thread_ids, n_threads
                        , TaskExecutor::threadExecute
                        , tp // Give the cmd to the thread (it will delete)
                        , flags
                        , priority
                        , this->grp_id()
                        , stack
                        , stack_size
                        , thread_handles
                        , this
                        , thr_name);
                } else {
                    // Thread Ids were not specified
                    grp_spawned = this->thr_mgr_->spawn_n(n_threads
                        , TaskExecutor::threadExecute
                        , tp // Give the cmd to the thread (it will delete)
                        , flags
                        , priority
                        , this->grp_id()
                        , this
                        , thread_handles
                        , stack
                        , stack_size
                        , thr_name);
                }

                if (grp_spawned == -1) {
                    for (size_t thr_count = n_threads; thr_count--;) {
                        --this->thr_count_; WorkerTask::intrusive_remove_ref(tp);
                    }
                    break;
                }

                this->last_thread_id_ = ACE_thread_t(0);    // Reset to prevent inadvertant match on ID
            }

            DAF_OS::thr_yield(); // Let the thread(s) start up

            return 0;

        } while (false);

        DAF_OS::last_error(ENOEXEC); return -1;
    }

    int
    TaskExecutor::execute(const Runnable_ref & command)
    {
        if (this->isAvailable()) try {

            if (!DAF::is_nil(command)) { // Empty command then we are all done!

                // What happens here is we attempt to offer the Runnable to an existing thread
                // We allow up to 'handoff_timeout_' milliseconds before creating a new thread for the pool
                if (this->taskChannel_.offer(command,this->handoff_timeout_) && this->task_handOff(command)) {
                    throw "Failed-Thread-HandOff";
                }
            }

            return 0;

        } DAF_CATCH_ALL {
            ACE_DEBUG((LM_ERROR,
                ACE_TEXT("DAF (%P | %t) ERROR: TaskExecutor:")
                ACE_TEXT(" Unable to hand-off executable command 0x%@.\n"), command.ptr()));
        }

        DAF_OS::last_error(ENOEXEC); return -1;
    }

    int
    TaskExecutor::task_dispatch(Runnable_ref command)
    {
        for (const ACE_Sched_Priority default_prio(DAF_OS::thread_PRIORITY()); this->isAvailable();) {

            if (DAF::is_nil(command)) try {
                command = this->taskChannel_.poll(this->getDecayTimeout())._retn(); continue;
            } catch (const std::runtime_error &) {
                break;
            }

            DAF_OS::thr_setprio(ACE_Sched_Priority(command->runPriority())); // Set the requested priority

            DAF_OS::last_error(0); this->svc(command._retn()); // Dispatch The Command

            DAF_OS::thr_setprio(default_prio);  // Reset Priority
        }

        command = Runnable::_nil(); return 0;
    }

    int
    TaskExecutor::task_handOff(const Runnable_ref & command)
    {
        if (DAF::is_nil(command) ? false : this->isAvailable()) do {

            WorkerTask_ref tp(new WorkerTaskExtended(this, command));

            {
                ACE_GUARD_REACTION(ACE_Thread_Mutex, mon, this->lock_, break);

                if (this->isAvailable()) { // DCL

                    ++this->thr_count_; WorkerTask::intrusive_add_ref(tp); // Increment permits by threadcount

                    int grp_spawned = this->thr_mgr()->spawn_n(1
                        , TaskExecutor::threadExecute
                        , tp // Give the cmd to the thread (it will delete)
                        , (THR_NEW_LWP | THR_JOINABLE | THR_INHERIT_SCHED)
                        , ACE_DEFAULT_THREAD_PRIORITY
                        , this->grp_id()
                        , this
                    );

                    if (grp_spawned == -1) {
                        --this->thr_count_; WorkerTask::intrusive_remove_ref(tp); break; // Clean up command after failed handoff
                    }

                    this->last_thread_id_ = ACE_thread_t(0);    // Reset to prevent inadvertant match on ID

                } else break; // Not Available
            }

            DAF_OS::thr_yield(); // Let the thread start up

            return 0;

        } while (false);

        return -1;
    }

    int
    TaskExecutor::module_closed(void)
    {
        this->executorAvailable_ = false; // Say we are no longer available

        if (this->executorClosed_ ? false : (this->executorClosed_ = true)) {

            ACE_Task_Base::module_closed(); this->taskChannel_.interrupt();

            try {

                ACE_GUARD_REACTION(ACE_Thread_Mutex, mon, this->lock_, throw LockFailureException());

                for (const ACE_Time_Value tv(DAF_OS::gettimeofday(this->getEvictTimeout())); this->thr_count() > 0;) {
                    if (this->zeroCondition_.wait(&tv) && DAF_OS::last_error() == ETIME) {
                        if (this->thr_count() > 0) { // DCL
                            throw TimeoutException();  // Throw to terminate_task(release locks)
                        } else break;
                    }
                }

            } DAF_CATCH_ALL { // Must be called without locks held
                for (Thread_Manager * thr_mgr = static_cast<Thread_Manager *>(this->thr_mgr()); thr_mgr;) {
                    thr_mgr->terminate_task(this, true); break;
                }
            }
        }

        return 0;
    }

    /*********************************************************************************/

    int
    TaskExecutor::WorkerTask::run(void)
    {
        return this->taskExecutor()->svc();
    }

    int
    TaskExecutor::WorkerTaskExtended::run(void)
    {
        return this->taskExecutor()->task_dispatch(this->command_._retn());
    }

    /*********************************************************************************/

    int
    TaskExecutor::Thread_Descriptor::threadAtExit(bool force_at_exit)
    {
        if (ACE_BIT_DISABLED(this->threadState(), ACE_Thread_Manager::ACE_THR_CANCELLED)) try {

            // Skip if thread has already been cancelled (terminated)

#if defined(DAF_HANDLES_THREAD_CLEANUP) && (DAF_HANDLES_THREAD_CLEANUP > 0)
            ACE_UNUSED_ARG(force_at_exit);
#else
            if (force_at_exit) // Set by threadTerminate()
#endif
            {
#if defined(ACE_HAS_THREAD_DESCRIPTOR_TERMINATE_ACCESS) && (ACE_HAS_THREAD_DESCRIPTOR_TERMINATE_ACCESS > 0)
                this->do_at_exit();
#else
                this->at_exit(this->taskBase(), 0, 0); TaskExecutor::threadCleanup(this->taskBase(), this);
#endif
            }

            return 0;

        } DAF_CATCH_ALL {
            // Encountered a User Code Error
        }

        return -1;
    }

    int
    TaskExecutor::Thread_Descriptor::threadTerminate(Thread_Manager * thr_mgr, int async_cancel)
    {
        // ACE_Thread::cancel under pthread will result in a pthread_cancel being
        // called. (See 'man pthread_cancel') which results in an async
        // roll back through the thread's stack via the abi::__forced_unwind
        // C++ exception (GCC/G++ special!!). During this process the pthread/ACE structures
        // correctly unwinds and deallocates the Thread Specific Storage (TSS/TLS)
        // which generally includes the ACE_Log_Msg, Service_Config, TAO POA elements
        // etc.

        if (this->threadAtExit(true) ? false : thr_mgr->cancel_thr(this, async_cancel)) { // Handle at_exits and if OK - Kill the thread

#if defined(ACE_WIN32)
            ACE_SET_BITS(this->threadFlags(), THR_DETACHED); // Set THR_DETACHED - Stops waiting on non-existant thread

            if (::TerminateThread(this->threadHandle(), DWORD(0xDEAD))) {
                DAF::threadSYNCHTerminate(this->self());
            }
#endif

#if defined(ACE_HAS_THREAD_DESCRIPTOR_TERMINATE_ACCESS) && (ACE_HAS_THREAD_DESCRIPTOR_TERMINATE_ACCESS > 0)
            this->terminate();
#else
            thr_mgr->remove_thr(this, async_cancel); // This *may* leave TSS leaking (Fixed with terminate() access)
#endif
        }

        return 0;
    }

    /*********************************************************************************/

    int
    TaskExecutor::Thread_Manager::terminate_task(ACE_Task_Base * task, int async_cancel)
    {
        ACE_GUARD_RETURN(ACE_Thread_Mutex, ace_mon, this->lock_, (DAF_OS::last_error(ENOLCK),-1));

        ACE_ASSERT(this->thr_to_be_removed_.is_empty());

        int result = 0;

        if (task) {
            for (ACE_Double_Linked_List_Iterator<ACE_Thread_Descriptor> iter(this->thr_list_); !iter.done(); iter.advance()) {
                Thread_Descriptor * td(static_cast<Thread_Descriptor *>(iter.next()));
                if (td && td->taskBase() == task && this->terminate_thr(td, async_cancel)) {
                    result = -1;
                }
            }
        }

        // Must terminate threads after we have traversed the thr_list_ to prevent clobbering thr_to_be_removed_'s integrity.

        for (ACE_Thread_Descriptor * td = 0; this->thr_to_be_removed_.dequeue_head(td) == 0; DAF_OS::thr_yield()) {
            static_cast<Thread_Descriptor *>(td)->threadTerminate(this, async_cancel);
        }

        return result;
    }

    int
    TaskExecutor::Thread_Manager::terminate_thr(Thread_Descriptor * td, int /* async_cancel */)
    {
        if (ACE_BIT_DISABLED(td->threadState(), (ACE_THR_JOINING | ACE_THR_CANCELLED | ACE_THR_TERMINATED))) {
            return this->thr_to_be_removed_.enqueue_tail(td);
        }
        return -1;
    }

    /*********************************************************************************/

    int SingletonExecute(const Runnable_ref & command)
    {
        struct SingletonExecutor : TaskExecutor
        {
            enum {
                SINGLETON_DECAY_TIMEOUT = (THREAD_DECAY_TIMEOUT / 2) // 15 Seconds
            };

            SingletonExecutor(void) : TaskExecutor()
            {
                this->setDecayTimeout(SINGLETON_DECAY_TIMEOUT);
            }

            const ACE_TCHAR * dll_name(void) const
            {
                return DAF_DLL_NAME;
            }

            const ACE_TCHAR * name(void) const
            {
                return typeid(*this).name();
            }
        };

        return ACE_DLL_Singleton_T<SingletonExecutor, ACE_SYNCH_MUTEX>::instance()->execute(command);
    }

}  // namespace DAF
