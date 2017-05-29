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
#include "ShutdownHandler.h"
#include "PropertyManager.h"

#include <limits>

#if defined (ACE_HAS_SIG_C_FUNC)
extern "C" void DAF_TaskExecutor_cleanup(void *obj, void *args)
{
    DAF::TaskExecutor::cleanup(obj, args);
}
#endif /* ACE_HAS_SIG_C_FUNC */

//#define DAF_USES_TERMINATE_THREAD  /* Breaks ACE_Object_Manager Singleton */

namespace DAF
{
    namespace { // Ananomous

        int make_grp_id(void *p)
        {
            return int(reinterpret_cast<size_t>(p) & size_t(std::numeric_limits<int>::max()));
        }

    } // Ananomous

    template <> inline DAF::Runnable_ref
    SynchronousChannel<DAF::Runnable_ref>::extract(void)
    {
        ACE_GUARD_REACTION(ACE_SYNCH_MUTEX, guard, *this, DAF_THROW_EXCEPTION(DAF::ResourceExhaustionException));
        DAF::Runnable_ref t(this->item_._retn()); this->itemTaken_.release();
        if (this->itemError_) {
            this->itemError_ = false; DAF_THROW_EXCEPTION(DAF::InternalException);
        }
        return t._retn();
    }

    /*********************************************************************************/

    ACE_THR_FUNC_RETURN
    TaskExecutor::execute_run(void *args)
    {
        DAF_OS::thread_0_SIGSET_T(); // Ignore all signals to avoid ERROR:

        TaskExecutor::WorkerExTask_ref worker(reinterpret_cast<TaskExecutor::WorkerExTask_ptr>(args));

        // This is exactly the same logics as ACE_Task_Base::svc_run however it works through a svc proxy

        if (worker) for (ACE_Task_Base * task(worker->task_base()); task;) {

            ACE_Thread_Descriptor * td = static_cast<TaskExecutor::ThreadManager *>(task->thr_mgr())->thread_desc_self();

            // Register ourself with our <Thread_Manager>'s thread exit hook
            // mechanism so that our close() hook will be sure to get invoked
            // when this thread exits.

#if defined(ACE_HAS_SIG_C_FUNC)
            td->at_exit(task, DAF_TaskExecutor_cleanup, td);
#else
            td->at_exit(task, DAF::TaskExecutor::cleanup, td);
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

#if defined(DAF_HANDLES_THREAD_CLEANUP) && (DAF_HANDLES_THREAD_CLEANUP == 1)
            if (ACE_BIT_DISABLED(static_cast<Thread_Descriptor *>(td)->threadState(), ACE_Thread_Manager::ACE_THR_TERMINATED)) {
                td->at_exit(task, 0, 0); TaskExecutor::cleanup(task, td); // This prevents a second invocation of the cleanup code
            }
#endif
            return status;
        }

        return ACE_THR_FUNC_RETURN(-1);
    }

    ACE_THR_FUNC_RETURN
    TaskExecutor::execute_svc(void *args)
    {
        DAF_OS::thread_0_SIGSET_T(); // Ignore all signals to avoid ERROR:

        for (ACE_Task_Base * task(reinterpret_cast<ACE_Task_Base *>(args)); task;) {

            ACE_Thread_Descriptor * td = static_cast<TaskExecutor::ThreadManager *>(task->thr_mgr())->thread_desc_self();

            // Register ourself with our <Thread_Manager>'s thread exit hook
            // mechanism so that our close() hook will be sure to get invoked
            // when this thread exits.

#if defined(ACE_HAS_SIG_C_FUNC)
            td->at_exit(task, DAF_TaskExecutor_cleanup, td);
#else
            td->at_exit(task, DAF::TaskExecutor::cleanup, td);
#endif /* ACE_HAS_SIG_C_FUNC */

            ACE_THR_FUNC_RETURN status = 0;

            try {  // Stop Application code from killing Server

                // Call the Task's run() hook method.
#if defined(ACE_HAS_INTEGRAL_TYPE_THR_FUNC_RETURN)
                // Reinterpret case between integral types is not mentioned in the C++ spec
                status = static_cast<ACE_THR_FUNC_RETURN>(task->svc());
#else
                status = reinterpret_cast<ACE_THR_FUNC_RETURN>(task->svc());
#endif /* ACE_HAS_INTEGRAL_TYPE_THR_FUNC_RETURN */

            } DAF_CATCH_ALL {
                if (ACE::debug() || DAF::debug()) {
                    ACE_DEBUG((LM_ERROR, ACE_TEXT("DAF (%P | %t) TaskExecutor ERROR: ")
                        ACE_TEXT("Unhandled exception caught from execute_svc() : TaskExecutor=0x%@.\n"), task));
                }
            }

#if defined(DAF_HANDLES_THREAD_CLEANUP) && (DAF_HANDLES_THREAD_CLEANUP == 1)
            if (ACE_BIT_DISABLED(static_cast<Thread_Descriptor *>(td)->threadState(), ACE_Thread_Manager::ACE_THR_TERMINATED)) {
                td->at_exit(task, 0, 0); TaskExecutor::cleanup(task, td); // This prevents a second invocation of the cleanup code
            }
#endif
            return status;
        }

        return ACE_THR_FUNC_RETURN(-1);
    }

    void
    TaskExecutor::cleanup(void *obj, void *args)
    {
        if (obj) for (TaskExecutor * task = dynamic_cast<TaskExecutor *>(reinterpret_cast<ACE_Task_Base *>(obj)); task;) {

            try { task->close(0); } DAF_CATCH_ALL { /* Calling application code */ }

            ACE_thread_t thr_self = ACE_Thread::self();

            {
                ACE_GUARD_REACTION(ACE_Thread_Mutex, ace_mon, task->lock_, break);

                if (args) for (Thread_Descriptor * td = reinterpret_cast<Thread_Descriptor*>(args); td;) {
                    thr_self = td->self();
                    if (DAF::debug() > 2) {
                        ACE_DEBUG((LM_INFO, ACE_TEXT("DAF (%P | %t) TaskExecutor::cleanup; ")
                            ACE_TEXT("grp_id=%d,thr_count=%d,ThreadID=%d[0x%X],ThreadState=0x%X\n")
                            , task->grp_id()
                            , task->thr_count()
                            , unsigned(thr_self), unsigned(thr_self)
                            , unsigned(td->threadState())));
                    }
                    break;
                }

                // Ensure we don't go negative (i.e. maybe because we are terminating threads)
                if (0 == --task->thr_count_) {
                    task->last_thread_id_ = thr_self;
                }

                task->zero_condition_.broadcast(); break; // Must Be Last as task may go away
            }
        }
    }

    /*********************************************************************************/

    TaskExecutor::ThreadManager TaskExecutor::threadManager_;

    TaskExecutor::TaskExecutor(void) : ACE_Task_Base(TaskExecutor::SingletonThreadManager::instance())
        , zero_condition_   (this->lock_)
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
        this->module_closed();
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
    TaskExecutor::svc(const DAF::Runnable_ref &cmd)
    {
        return (DAF::is_nil(cmd) ? 0 : cmd->run()); // Run the Command
    }

    bool
    TaskExecutor::isAvailable(void) const
    {
        if (this->executorAvailable_ && DAF::ShutdownHandler::has_shutdown()) {
            this->executorAvailable_ = false;
        }
        return this->executorAvailable_;
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
    TaskExecutor::execute(size_t  n_threads,
        bool    force_active,
        long    flags,
        long    priority,
        ACE_hthread_t thread_handles[],
        void *  stack[],
        size_t  stack_size[],
        ACE_thread_t thread_ids[],
        const char* thr_name[])
    {
        if (this->isAvailable()) {

            ACE_GUARD_RETURN(ACE_Thread_Mutex, ace_mon, this->lock_, -1);

            if (this->isAvailable()) do { // DCL

                if (force_active ? false : this->isActive()) {
                    break; // Already active.
                }

                this->thr_count_ += n_threads;

                int grp_spawned = -1;

                if (thread_ids) {
                    // thread names were specified
                    grp_spawned = this->thr_mgr_->spawn_n(thread_ids, n_threads,
                        &TaskExecutor::execute_svc,
                        this,
                        flags,
                        priority,
                        this->grp_id(),
                        stack,
                        stack_size,
                        thread_handles,
                        this,
                        thr_name);
                }
                else
                {
                    // Thread Ids were not specified
                    grp_spawned = this->thr_mgr_->spawn_n(n_threads,
                        &TaskExecutor::execute_svc,
                        this,
                        flags,
                        priority,
                        this->grp_id(),
                        this,
                        thread_handles,
                        stack,
                        stack_size,
                        thr_name);
                }

                if (grp_spawned == -1) {
                    this->thr_count_ -= n_threads; break;
                }

#if defined(ACE_TANDEM_T1248_PTHREADS)
                DAF_OS::memcpy(&this->last_thread_id_, 0, sizeof(this->last_thread_id_));
#else
                this->last_thread_id_ = 0;    // Reset to prevent inadvertant match on ID
#endif /* defined (ACE_TANDEM_T1248_PTHREADS) */

                DAF_OS::thr_yield(); return 0; // Let The Thread(s) start up (Still Locked though)

            } while (false);
        }

        return -1;
    }

    int
    TaskExecutor::execute(const DAF::Runnable_ref &cmd)
    {
        if (this->isAvailable()) try {

            if (!DAF::is_nil(cmd)) { // Empty command then we are all done!

                // What happens here is we attempt to offer the Runnable to an existing thread
                // We allow up to 'handoff_timeout_' milliseconds before creating a new thread for the pool
                if (this->taskChannel_.offer(cmd,this->handoff_timeout_) && this->task_handOff(cmd)) {
                    throw "Failed-Thread-HandOff";
                }
            }

            return 0;

        } DAF_CATCH_ALL {
            ACE_DEBUG((LM_ERROR,
                ACE_TEXT("DAF (%P | %t) ERROR: TaskExecutor:")
                ACE_TEXT(" Unable to hand-off executable command 0x%@.\n"), cmd.ptr()));
        }

        return -1;
    }

    int
    TaskExecutor::task_dispatch(DAF::Runnable_ref cmd)
    {
        for (const ACE_Sched_Priority default_prio(DAF_OS::thread_PRIORITY()); this->isAvailable();) {

            if (DAF::is_nil(cmd)) try {
                cmd = this->taskChannel_.poll(this->getDecayTimeout())._retn(); continue;
            } catch (const std::runtime_error &) {
                break;
            }

            DAF_OS::thr_setprio(ACE_Sched_Priority(cmd->runPriority())); // Set the requested priority

            DAF_OS::last_error(0); this->svc(cmd._retn()); // Dispatch The Command

            DAF_OS::thr_setprio(default_prio);  // Reset Priority
        }

        cmd = DAF::Runnable::_nil(); return 0;
    }

    int
    TaskExecutor::task_handOff(const DAF::Runnable_ref &cmd)
    {
        if (this->isAvailable()) do {
            {
                ACE_GUARD_REACTION(ACE_Thread_Mutex, ace_mon, this->lock_, break);

                if (this->isAvailable()) { // DCL

                    ++this->thr_count_;

                    WorkerExTask_ptr tp(new WorkerExTask(this, cmd));

                    int grp_spawned = this->thr_mgr()->spawn_n(1
                        , &TaskExecutor::execute_run
                        , tp // Give the cmd to the thread (it will delete)
                        , (THR_NEW_LWP | THR_JOINABLE | THR_INHERIT_SCHED)
                        , ACE_DEFAULT_THREAD_PRIORITY
                        , this->grp_id()
                        , this
                    );

                    if (grp_spawned == -1) {
                        delete tp; --this->thr_count_; break; // Clean up command after failed handoff
                    }

#if defined(ACE_TANDEM_T1248_PTHREADS)
                    DAF_OS::memset(&this->last_thread_id_, 0, sizeof(this->last_thread_id_));
#else
                    this->last_thread_id_ = 0;    // Reset to prevent inadvertant match on ID
#endif /* defined (ACE_TANDEM_T1248_PTHREADS) */
                }
            }

            DAF_OS::thr_yield(); return 0; // Let The Thread start up

        } while (false);

        return -1;
    }

    int
    TaskExecutor::module_closed(void)
    {
        this->executorAvailable_ = false; // Say we are no longer available

        if (this->executorClosed_ ? false : this->executorClosed_ = true) {

            ACE_Task_Base::module_closed(); this->taskChannel_.interrupt();

            const ACE_Time_Value tv(DAF_OS::gettimeofday(this->getEvictTimeout()));

            try {

                ACE_GUARD_RETURN(ACE_Thread_Mutex, mon, this->lock_, (DAF_OS::last_error(ENOLCK), -1));

                while (this->thr_count()) {
                    if (this->zero_condition_.wait(&tv) && this->thr_count()) { // DCL
                        throw "Threads-Not-Exiting";  // Throw to terminate_task(release locks)
                    }
                }

            } DAF_CATCH_ALL { // Must be called without locks held
                if (static_cast<Thread_Manager *>(this->thr_mgr())->terminate_task(this, true) == 0) {
                    this->wait();
                }
            }
        }

        return 0;
    }

    /*********************************************************************************/

    int
    TaskExecutor::WorkerExTask::run(void)
    {
        for (TaskExecutor *tex(dynamic_cast<TaskExecutor*>(this->task_base())); tex;) {
            return tex->task_dispatch(this->cmd_._retn());
        }

        this->cmd_ = DAF::Runnable::_nil(); return -1;
    }

    /*********************************************************************************/

    int
    TaskExecutor::Thread_Descriptor::threadTerminate(Thread_Manager *thr_mgr)
    {
        ACE_UNUSED_ARG(thr_mgr); // Not Used on Linux

        const ACE_thread_t thr_id = this->threadID(); // Save Thread ID for reporting

        // ACE_Thread::cancel under pthread will result in a pthread_cancel being
        // called. (See 'man pthread_cancel') which results in an async
        // roll back through the thread's stack via the abi::__forced_unwind
        // C++ exception (GCC/G++ special!!). During this process the pthread/ACE structures
        // correctly unwinds and deallocates the Thread Specific Storage (TSS/TLS)
        // which generally includes the ACE_Log_Msg, Service_Config, TAO POA elements
        // etc.

#if defined(ACE_HAS_THREAD_DESCRIPTOR_TERMINATE_ACCESS) && (ACE_HAS_THREAD_DESCRIPTOR_TERMINATE_ACCESS > 0)
        this->do_at_exit();
#endif
        if (DAF_OS::thr_cancel(thr_id)) {

#if defined(ACE_WIN32)

//            if (::TerminateThread(this->threadHandle(), DWORD(0xDEAD)))
            {
                ACE_SET_BITS(this->threadFlags(), THR_DETACHED); // Allows CloseHandle()
                ACE_SET_BITS(this->threadState(), ACE_Thread_Manager::ACE_THR_TERMINATED); // Stops cleanup logic

# if defined(ACE_HAS_THREAD_DESCRIPTOR_TERMINATE_ACCESS) && (ACE_HAS_THREAD_DESCRIPTOR_TERMINATE_ACCESS > 0)
                this->terminate();
# else
                this->at_exit(this->taskBase(), 0, 0); // Ensure we don't do the at_exit()

                TaskExecutor::cleanup(this->taskBase(), this);

                thr_mgr->remove_thr(this, 1);// This may leave TSS leaking (Fixed with terminate() access)
# endif
            }
#endif
            return -1; // Indicate we forced terminated (stops Task_Base::wait())
        }

        return 0;
    }

    /*********************************************************************************/

    int
    TaskExecutor::Thread_Manager::terminate_task(ACE_Task_Base * task, int async_cancel)
    {
        ACE_MT(ACE_GUARD_RETURN(ACE_Thread_Mutex, ace_mon, this->lock_, -1));
        ACE_ASSERT(this->thr_to_be_removed_.is_empty());

        int result = 0;

        if (task) for (ACE_Double_Linked_List_Iterator<ACE_Thread_Descriptor> iter(this->thr_list_); !iter.done(); iter.advance()) {
            Thread_Descriptor * td(static_cast<Thread_Descriptor *>(iter.next()));
            if (td && td->taskBase() == task) {
                this->terminate_thr(td, async_cancel);
            }
        }

        // Must terminate threads after we have traversed the thr_list_ to prevent clobbering thr_to_be_removed_'s integrity.

        for (ACE_Thread_Descriptor * ace_td = 0; this->thr_to_be_removed_.dequeue_head(ace_td) != -1; DAF_OS::thr_yield()) {
            Thread_Descriptor * td = static_cast<Thread_Descriptor *>(ace_td);
            if (td && td->threadTerminate(this)) {
                result = -1;
            }
        }

        return result;
    }

    int
    TaskExecutor::Thread_Manager::terminate_thr(Thread_Descriptor * td, int /* async_cancel */)
    {
        if (ACE_BIT_DISABLED(td->threadState(), (ACE_THR_JOINING | ACE_THR_TERMINATED))) {
            return this->thr_to_be_removed_.enqueue_tail(td);
        }
        return -1;
    }

    /*********************************************************************************/

    int SingletonExecute(const DAF::Runnable_ref &command)
    {
        static struct _TaskExecutor : DAF::TaskExecutor {
            _TaskExecutor(void) : DAF::TaskExecutor() {
                this->setDecayTimeout(THREAD_DECAY_TIMEOUT / 2);
            }
        } taskExecutor_;

        return taskExecutor_.execute(command);
    }

}  // namespace DAF
