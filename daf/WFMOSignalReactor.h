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

#ifndef DAF_WFMOSIGNALREACTOR_H
#define DAF_WFMOSIGNALREACTOR_H


#include "DAF.h"
#include "TaskExecutor.h"

#include <ace/Reactor.h>
#include <ace/Singleton.h>
#include <ace/Timer_Heap.h>
#include <atomic>

namespace DAF
{

#ifdef DAF_USES_SIM_TIME
    class DAF_Time_Policy; // fwd decl for template
    using DAF_Time_Value = ACE_Time_Value_T<DAF_Time_Policy>;
    class DAF_Export DAF_Time_Policy
    {
    public:
        /// Return the current time according to this policy
        DAF_Time_Value operator()() const;
        /// Noop. Just here to satisfy backwards compatibility demands.
        void set_gettimeofday(ACE_Time_Value(*)()) {}
    };
#else
    using DAF_Time_Policy = ACE_System_Time_Policy;
    using DAF_Time_Value = ACE_Time_Value_T<DAF_Time_Policy>;
#endif

    // template to redefine the order of the template params so that we can still use defaults but swap the time policy below
    template <typename TIME_POLICY = ACE_System_Time_Policy,
        typename TYPE = std::remove_reference_t<decltype(std::declval<std::remove_pointer_t<decltype(std::declval<ACE_Timer_Heap::HEAP_ITERATOR>().item())>>().get_type())>,
        typename FUNCTOR = std::remove_reference_t<decltype(std::declval<ACE_Timer_Heap::Base_Time_Policy>().upcall_functor())>,
        typename ACE_LOCK = std::remove_reference_t<decltype(std::declval<ACE_Timer_Heap>().mutex())>>
        using Timer_Queue = ACE_Timer_Heap_T<TYPE, FUNCTOR, ACE_LOCK, TIME_POLICY>;

    // Use via the ACE Singleton, SingletonSimTime::instance()->set_time(...);
    class DAF_Export DAFTime
    {
        std::atomic<double> time_ = 0.0;
    public:
        // explicitly set the time
        void set_time(double time);
        // return the current time
        double get_time() const;
        DAF_Time_Value get_time_value() const;
        // step to the next timer tick, or not if no next
        void tick();
        // get the next timer tick, or current time if no next
        double get_next_tick() const;
    };

    struct DAF_Export DAFTimeSingleton : DAFTime
    {
        const ACE_TCHAR *dll_name() const
        {
            return DAF_DLL_NAME;
        }
        const ACE_TCHAR* name() const
        {
            return typeid(*this).name();
        }
    };

} // namespace DAF

// sim time singleton
typedef ACE_DLL_Singleton_T<DAF::DAFTimeSingleton, ACE_SYNCH_MUTEX> SingletonDAFTime;
DAF_SINGLETON_DECLARE(ACE_DLL_Singleton_T, DAF::DAFTimeSingleton, ACE_SYNCH_MUTEX)

namespace DAF
{

    /** @class WFMOSignalReactor
    * @brief Brief \todo{Fill this in}
    *
    * Details \todo{Detailed description}
    */
    class DAF_Export WFMOSignalReactor : public ACE_Reactor
        , public DAF::TaskExecutor
    {
        using ACE_Reactor::close; // Scope this to private - Ambiguity with ACE_Task_Base

    public:

        /** \todo{Fill this in} */
        enum {
            DEFAULT_THREAD_COUNT = 5
        };

        /** \todo{Fill this in} */
        WFMOSignalReactor(bool use_sim_time_policy);
        /** \todo{Fill this in} */
        virtual ~WFMOSignalReactor();

        /** \todo{Fill this in} */
        virtual int run(size_t thr_count, bool wait_completion = false);

    protected:

        /** \todo{Fill this in} */
        virtual int close(u_long flags);

        /** \todo{Fill this in} */
        virtual int svc();

    protected:

        /// Terminates object when dynamic unlinking occurs.
        virtual int fini();
    };

    /** @struct WFMOSignalReactorSingleton
    * @brief Brief \todo{Fill this in}
    *
    * Details \todo{Detailed description}
    */
    struct DAF_Export WFMOSignalReactorSingleton : WFMOSignalReactor
    {
        /** \todo{Fill this in} */
        WFMOSignalReactorSingleton();

        /** \todo{Fill this in} */
        const ACE_TCHAR *dll_name() const
        {
            return DAF_DLL_NAME;
        }

        /** \todo{Fill this in} */
        const ACE_TCHAR *name() const
        {
            return typeid(*this).name();
        }
    };
} // namespace DAF

typedef DAF::WFMOSignalReactorSingleton DAF_WFMOSignalReactorSingleton;
typedef ACE_DLL_Singleton_T<DAF_WFMOSignalReactorSingleton, ACE_SYNCH_MUTEX>    SingletonWFMOSignalReactor;

// This is needed to get only one of these defined across a set of DLLs and EXE
DAF_SINGLETON_DECLARE(ACE_DLL_Singleton_T, DAF_WFMOSignalReactorSingleton, ACE_SYNCH_MUTEX)

#endif // DAF_WFMOSIGNALREACTOR_H
