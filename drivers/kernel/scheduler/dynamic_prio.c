#include <linux/sched.h>
#include <linux/module.h>

static unsigned int boost_factor =2;
module_param(boost_factor, uint, 0644);

static struct task_struct *find_can_task(void)
{
    struct task_struct *p;

    for_each_process(p) 
    {
        if (strncmp(p->comm, "canbus", 6) == 0)
            return p;
    }

    return NULL;
}

static void adjust_priority(struct work_struct *work)
{
    struct task_struct *can_task = find_can_task();

    if (can_task)
    {
        set_user_nice(can_task, MAX_NICE - boost_factor);
    }
}

DECLARE_WORK(prio_wrok, adjust_priority);
static struct timer_list prio_timer;

static void timer_callback(struct timer_list *data)
{
    schedule_work(&prio_wrok);
    mod_timer(&prio_timer, jiffies + msecs_to_jiffies(100));
}

static int __init dyn_prio_init(void)
{
    timer_setup(&prio_timer, timer_callback,0);
    mod_timer(&prio_timer,jiffies + msecs_to_jiffies(100));
    return 0;
}


module_init(dyn_prio_init);
module_exit(dyn_prio_exit);