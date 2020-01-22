#include <pmc/hl_events.h>
#include <pmc/mc_experiments.h>
#include <pmc/monitoring_mod.h>
#include <pmc/pmu_config.h> //For cpuid_regs_t
#include <linux/rbtree.h>
#include <linux/sched/deadline.h>
#include <linux/sched/rt.h>

#define LLC_MODEL_STRING "LLC cache-misses monitoring module"


/*Global PMC config for each task in the system*/
const char *llc_monitoring_pmcstr_cfg[] = {
	"pmc3=0x2e,umask3=0x41,pmc1", 
	NULL};

/* Unitary array of experiment sets where to copy the global 
configuration just once when the module is loaded */
core_experiment_set_t llc_monitoring_pmc_configuration[1];

/*Global metric containing llc misses and prefetches data 
(need to have a metric for PMCTrack structure)*/
metric_experiment_t llc_monitoring_metric_exp;

/*Per-task private data */
typedef struct {
	metric_experiment_t llc_metric;
	unsigned int samples_cnt;   // number of samples
	uint64_t cur_llc_miss_rate;	// LLC_miss/cycles
	uint64_t cur_llc_misses;	// LLC_miss count
	char is_new_sample;			// ready sample flag 
}llc_monitoring_thread_data_t;

/*Initialise LLC metric; this is called when the module is loaded*/
static void init_llc_metric(metric_experiment_t *metric_exp)
{
	pmc_metric_t *cmr_metric = NULL;
	pmc_metric_t *cm_metric = NULL;
	init_metric_experiment_t(metric_exp, 0);
	cmr_metric = &(metric_exp->metrics[0]);
	cm_metric = &(metric_exp->metrics[1]);
	metric_exp->size = 2; //2 HW Counters involved (to double check) 
	pmc_arg_t arguments[2];
	arguments[0].index = 0;
	arguments[0].type = hw_event_arg;
	arguments[1].index = 1;
	arguments[1].type = hw_event_arg;
	init_pmc_metric(cmr_metric, "LLC_miss_rate", op_rate, arguments, 100);
	init_pmc_metric(cm_metric, "LLC_miss_count", op_none, arguments, 1);
	
}
/*
static void llc_monitoring_module_counter_usage(monitoring_module_counter_usage_t* usage)
{
	usage->hwpmc_mask=0x0;

	usage->nr_virtual_counters=2; 
	usage->nr_experiments = 1;
	usage->vcounter_desc[0] = "LLC_miss_rate";
	usage->vcounter_desc[1] = "LLC_miss_count";
}
*/
/*Configure the global experiment set to enable the counters 
to count LLC misses and prefetches and initialise the global metric for LLC*/
static int llc_monitoring_enable_module(void)
{
	if (configure_performance_counters_set(llc_monitoring_pmcstr_cfg, 
										   llc_monitoring_pmc_configuration, 1)) {
		printk("Cannot configure global performance counters...\n");
		return -EINVAL;
	}
	init_llc_metric(&llc_monitoring_metric_exp);
	printk(KERN_ALERT "%s has been loaded successfuly\n", LLC_MODEL_STRING);

	/*
	printk("Operation mode = %d (should be 6) | ID : %s | Size of metric_exp = %u | exp_idx = %d\n",
	 llc_monitoring_metric_exp.metrics[0].mode, llc_monitoring_metric_exp.metrics[0].id, 
	 llc_monitoring_metric_exp.size, llc_monitoring_metric_exp.exp_idx);
	*/
	return 0;
}


static void llc_monitoring_disable_module(void)
{
	free_experiment_set(&llc_monitoring_pmc_configuration[0]);
	printk(KERN_ALERT "%s monitoring module unloaded!!\n", LLC_MODEL_STRING);
}


static int llc_monitoring_on_fork(unsigned long clone_flags, pmon_prof_t *prof)
{
	llc_monitoring_thread_data_t *data = NULL;

	if (prof->monitoring_mod_priv_data != NULL)
		return 0;

	/*Clone global experiment set, i.e. counters configuration, in child process private data*/
	if (!prof->pmcs_config){
		clone_core_experiment_set_t(&prof->pmcs_multiplex_cfg[0], 
									llc_monitoring_pmc_configuration);
		/*Update current experiment set, i.e. counters configuration, of child process*/
		prof->pmcs_config = get_cur_experiment_in_set(&prof->pmcs_multiplex_cfg[0]);
	}

	data = kmalloc(sizeof(llc_monitoring_thread_data_t), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	memcpy(&data->llc_metric, &llc_monitoring_metric_exp, sizeof(metric_experiment_t));
	data->samples_cnt = 0;
	data->cur_llc_misses = 1;
	data->cur_llc_miss_rate = 1;
	data->is_new_sample = 0;
	//prof->nticks_sampling_period = HZ;
	prof->monitoring_mod_priv_data = data;
	if (!(dl_prio(prof->this_tsk->prio) || rt_prio(prof->this_tsk->prio)))
		prof->this_tsk->prof_enabled = 1;

	return 0;
}


static int llc_monitoring_on_new_sample(pmon_prof_t *prof, int cpu, pmc_sample_t *sample, int flags, void *data)
{
	llc_monitoring_thread_data_t *llc_data;
	llc_data = (llc_monitoring_thread_data_t*)prof->monitoring_mod_priv_data;
	int cnt_virt = 0;
	int i;
	if (llc_data != NULL) {
		metric_experiment_t *metric_exp = &llc_data->llc_metric;
		compute_performance_metrics(sample->pmc_counts, metric_exp);		
		llc_data->cur_llc_miss_rate = metric_exp->metrics[0].count;
		llc_data->cur_llc_misses = metric_exp->metrics[1].count;
		llc_data->samples_cnt++;
		
		/* Embed virtual counter information so that the user can see what's going on */
/*		for(i = 0; i < 2; i++) {
			if ((prof->virt_counter_mask & (1<<i) )) {
				sample->virt_mask |= (1<<i);
				sample->nr_virt_counts++;
				if (cnt_virt == 0)
					sample->virtual_counts[cnt_virt] = llc_data->cur_llc_miss_rate;
				else if (cnt_virt == 1)
					sample->virtual_counts[cnt_virt] = llc_data->cur_llc_misses;
				else
					;
				cnt_virt++;
			}
		}*/
		llc_data->is_new_sample = 1;
		trace_printk("cm_rate,%llu,cm,%llu\n", llc_data->cur_llc_miss_rate, llc_data->cur_llc_misses);
	}

	return 0;
}



/*Deallocate private data*/
static void llc_monitoring_on_free_task(pmon_prof_t *prof)
{
	llc_monitoring_thread_data_t* data;
	data = (llc_monitoring_thread_data_t*)prof->monitoring_mod_priv_data;
	if (data)
		kfree(data);
}


static int llc_monitoring_get_current_metric_value(pmon_prof_t *prof, int key, uint64_t *value)
{
	llc_monitoring_thread_data_t *llc_data;
	llc_data = (llc_monitoring_thread_data_t*)prof->monitoring_mod_priv_data;

	if (llc_data == NULL)
		return -1;

	if (key == CACHE_MISS_RATE && llc_data->is_new_sample){
		(*value) = llc_data->cur_llc_miss_rate;
		llc_data->is_new_sample = 0;
		return 0;
	}
	else
		return -1;

}
/*
static void llc_monitoring_on_switch_out(pmon_prof_t *prof)
{
	llc_monitoring_thread_data_t *llc_data;
	llc_data = (llc_monitoring_thread_data_t*)prof->monitoring_mod_priv_data;

	if (llc_data)
		trace_printk("Process %d has been preempted\n", prof->this_tsk->pid);
}
*/

/*
static void llc_monitoring_on_exit(pmon_prof_t *prof)
{
	llc_monitoring_thread_data_t *llc_data;
	llc_data = (llc_monitoring_thread_data_t*)prof->monitoring_mod_priv_data;
	if (llc_data)
		trace_printk("Process %d exiting...\n", prof->this_tsk->pid);
}
*/
/*
static void llc_monitoring_on_tick(pmon_prof_t *prof, int cpu)
{
	llc_monitoring_thread_data_t *llc_data = prof->monitoring_mod_priv_data;
		
	if (llc_data && llc_data->is_new_sample){
		//trace_printk("PID = %d LLC_MISSES[%u] = %llu\n", prof->this_tsk->pid, llc_data->samples_cnt, llc_data->cur_llc_misses);
		llc_data->is_new_sample = 0;
	}
}
*/

/* Implementation of the monitoring_module_t interface */
monitoring_module_t llc_monitoring_mm = {
	.info = LLC_MODEL_STRING,
	.id = -1,
	.enable_module = llc_monitoring_enable_module,
	.disable_module = llc_monitoring_disable_module,
	.on_fork = llc_monitoring_on_fork,
	.on_free_task = llc_monitoring_on_free_task,
	.on_new_sample = llc_monitoring_on_new_sample,
	.get_current_metric_value = llc_monitoring_get_current_metric_value,
	//.module_counter_usage = llc_monitoring_module_counter_usage
	//.on_switch_out = llc_monitoring_on_switch_out,
	//.on_tick = llc_monitoring_on_tick
	//.on_migrate = llc_monitoring_on_migrate
	/*
	.on_read_config=ipc_sampling_on_read_config,
	.on_write_config=ipc_sampling_on_write_config,
	
	.on_exec=ipc_sampling_on_exec,
	.on_new_sample=ipc_sampling_on_new_sample,
	
	.on_free_task=ipc_sampling_on_free_task,
	.get_current_metric_value=ipc_sampling_get_current_metric_value,
	
	*/
};