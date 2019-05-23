/***************************************************************
    Copyright 2019 Defence Science and Technology Group,
    Department of Defence,
    Commonwealth of Australia

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
#define DAF_WFMOSIGNALREACTOR_CPP

#include "daf/DAF.h"
#include "daf/DAFDebug.h"
#include "daf/WFMOSignalReactor.h"

#if defined(ACE_WIN32)
#include <ace/WFMO_Reactor.h>
#else
#include <ace/TP_Reactor.h>
#endif

#include <ace/Log_Msg.h>
#include <ace/Timer_Heap.h>

namespace  //anonymous
{

    class Sim_Time_Policy;
    using Sim_Time_Value = ACE_Time_Value_T<Sim_Time_Policy>;
    class Sim_Time_Policy
    {
    public:
        /// Return the current time according to this policy
        Sim_Time_Value operator()() const; // todo

        /// Noop. Just here to satisfy backwards compatibility demands.
        void set_gettimeofday(ACE_Time_Value(*)()) {}
    };

    // template to redefine the order of the template params so that we can still use defaults but swap the time policy below
    template <typename TIME_POLICY = ACE_System_Time_Policy,
        typename TYPE = std::remove_reference_t<decltype(std::declval<std::remove_pointer_t<decltype(std::declval<ACE_Timer_Heap::HEAP_ITERATOR>().item())>>().get_type())>,
        typename FUNCTOR = std::remove_reference_t<decltype(std::declval<ACE_Timer_Heap::Base_Time_Policy>().upcall_functor())>,
        typename ACE_LOCK = std::remove_reference_t<decltype(std::declval<ACE_Timer_Heap>().mutex())>>
    using Timer_Queue = ACE_Timer_Heap_T<TYPE, FUNCTOR, ACE_LOCK, TIME_POLICY>;

#if defined (ACE_WIN32)
    struct WFMO_Reactor : ACE_WFMO_Reactor {
        using ACE_WFMO_Reactor::ACE_WFMO_Reactor;
        virtual int expire_timers() {
            for (ACE_Timer_Queue *tq = this->timer_queue(); tq;) {
                return (this->deactivated() ? 0 : tq->expire()); // Allow any thread to dispatch timers
            }
            return 0;
        }
    };
#else
    typedef class ACE_TP_Reactor WFMO_Reactor;
#endif

} //namespace anonymous

namespace DAF
{
    WFMOSignalReactor::WFMOSignalReactor(bool use_sim_time_policy)
        : ACE_Reactor(new WFMO_Reactor(nullptr, use_sim_time_policy
                                                ? static_cast<ACE_Timer_Queue*>(new Timer_Queue<Sim_Time_Policy>())
                                                : static_cast<ACE_Timer_Queue*>(new Timer_Queue<>())), true)
    {
    }

    WFMOSignalReactor::~WFMOSignalReactor()
    {
        this->TaskExecutor::module_closed();
        this->ACE_Task_Base::wait();
    }

    int WFMOSignalReactor::run(size_t thr_count, bool wait_completion)
    {
        if (this->initialized())
        {
            if (this->execute(thr_count = ace_range(size_t(1), size_t(100), thr_count), true) == -1)
            {
                ACE_ERROR_RETURN((LM_ERROR, ACE_TEXT("DAF (%04P | %04t) ERROR: Failed to activate SignalReactor threads.\n")), -1);
            }
            if (DAF::debug() > 1)
            {
                ACE_DEBUG((LM_INFO, ACE_TEXT("DAF (%04P | %04t) INFO: SignalReactor started with %u threads.\n"), thr_count));
            }
        }
        else
        {
            ACE_ERROR_RETURN((LM_ERROR, ACE_TEXT("DAF (%04P | %04t) ERROR: SignalReactor - Not Initialized.\n")), -1);
        }

        return wait_completion ? this->wait() : 0;
    }

    int WFMOSignalReactor::svc()
    {
        while (this->isAvailable())
        {
            return this->run_reactor_event_loop();
        }
        return -1;
    }

    int WFMOSignalReactor::close(u_long flags)
    {
        if (flags)
        {
            if (this->implementation()->deactivated() || this->end_reactor_event_loop() == 0)
            {
                DAF::TaskExecutor::close(flags);
            }
            return this->implementation()->close();
        }

        return DAF::TaskExecutor::close(flags);
    }

    int WFMOSignalReactor::fini()
    {
        return this->module_closed();
    }

    /**************************************************************************/

    WFMOSignalReactorSingleton::WFMOSignalReactorSingleton() : WFMOSignalReactor(false)
    {
        this->WFMOSignalReactor::run(DEFAULT_THREAD_COUNT, false);
    }

} // namespace DAF
