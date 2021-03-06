/* parse_record.c  */
/* by Vince Weaver   vincent.weaver _at_ maine.edu */

/* This code parses a perf event trace record */

#define _GNU_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <unistd.h>
#include <fcntl.h>

#include <errno.h>

#include <signal.h>

#include <sys/mman.h>

#include <sys/ioctl.h>
#include <asm/unistd.h>
#include <sys/prctl.h>

#include "../include/perf_event.h"

#include "parse_record.h"
#include "data_read.h"

#if defined(__x86_64__) || defined(__i386__) ||defined(__arm__)
#include <asm/perf_regs.h>
#endif

/* Urgh who designed this interface */
static int handle_struct_read_format(unsigned char *sample,
				     int read_format) {

	int offset=0,i;

	if (read_format & PERF_FORMAT_GROUP) {
		long long nr,time_enabled,time_running;

		memcpy(&nr,&sample[offset],sizeof(long long));
		printf("\t\tNumber: %lld ",nr);
		offset+=8;

		if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED) {
			memcpy(&time_enabled,&sample[offset],sizeof(long long));
			printf("enabled: %lld ",time_enabled);
			offset+=8;
		}
		if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING) {
			memcpy(&time_running,&sample[offset],sizeof(long long));
			printf("running: %lld ",time_running);
			offset+=8;
		}

		printf("\n");

		for(i=0;i<nr;i++) {
			long long value, id;

			memcpy(&value,&sample[offset],sizeof(long long));
			printf("\t\t\tValue: %lld ",value);
			offset+=8;

			if (read_format & PERF_FORMAT_ID) {
				memcpy(&id,&sample[offset],sizeof(long long));
				printf("id: %lld ",id);
				offset+=8;
			}

			printf("\n");
		}
	}
	else {

		long long value,time_enabled,time_running,id;

		memcpy(&value,&sample[offset],sizeof(long long));
		printf("\t\tValue: %lld ",value);
		offset+=8;

		if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED) {
			memcpy(&time_enabled,&sample[offset],sizeof(long long));
			printf("enabled: %lld ",time_enabled);
			offset+=8;
		}
		if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING) {
			memcpy(&time_running,&sample[offset],sizeof(long long));
			printf("running: %lld ",time_running);
			offset+=8;
		}
		if (read_format & PERF_FORMAT_ID) {
			memcpy(&id,&sample[offset],sizeof(long long));
			printf("id: %lld ",id);
			offset+=8;
		}
		printf("\n");
	}

	return offset;
}

#if defined(__x86_64__)

#define NUM_REGS	PERF_REG_X86_64_MAX
static char reg_names[NUM_REGS][8]=
			{"RAX","RBX","RCX","RDX","RSI","RDI","RBP","RSP",
			 "RIP","RFLAGS","CS","SS","DS","ES","FS","GS",
			 "R8","R9","R10","R11","R12","R13","R14","R15"};


#elif defined(__i386__)

#define NUM_REGS	PERF_REG_X86_32_MAX
static char reg_names[PERF_REG_X86_32_MAX][8]=
			{"EAX","EBX","ECX","EDX","ESI","EDI","EBP","ESP",
			 "EIP","EFLAGS","CS","SS","DS","ES","FS","GS"};

#elif defined(__arm__)

#define NUM_REGS	PERF_REG_ARM_MAX
static char reg_names[PERF_REG_ARM_MAX][8]=
			{"R0","R1","R2","R3","R4","R5","R6","R7",
			 "R8","R9","R10","FP","IP","SP","LR","PC"};

#else

#define NUM_REGS 0

static char reg_names[1][8]={"NONE!"};

#endif



static int print_regs(long long abi,long long reg_mask,
		unsigned char *data) {

	int return_offset=0;
	int num_regs=NUM_REGS;
	int i;
	unsigned long long reg_value;

	printf("\t\tReg mask %llx\n",reg_mask);
	for(i=0;i<64;i++) {
		if (reg_mask&1ULL<<i) {
			memcpy(&reg_value,&data[return_offset],8);
			if (i<num_regs) {
				printf("\t\t%s : ",reg_names[i]);
			}
			else {
				printf("\t\t??? : ");
			}

			printf("%llx\n",reg_value);
			return_offset+=8;
		}
	}

	return return_offset;
}


static int dump_raw_ibs_fetch(unsigned char *data, int size) {

	unsigned long long *msrs;
	unsigned int *leftover;

	msrs=(unsigned long long *)(data+4);
	leftover=(unsigned int *)(data);

	printf("\t\tHeader: %x\n",leftover[0]);
	printf("\t\tMSR IBS_FETCH_CONTROL %llx\n",msrs[0]);
	printf("\t\t\tIBS_RAND_EN: %d\n",!!(msrs[0]&1ULL<<57));
	printf("\t\t\tL2 iTLB Miss: %d\n",!!(msrs[0]&1ULL<<56));
	printf("\t\t\tL1 iTLB Miss: %d\n",!!(msrs[0]&1ULL<<55));
	printf("\t\t\tL1TLB page size: ");
	switch( (msrs[0]>>53)&0x3) {
		case 0:	printf("4kB\n"); break;
		case 1:	printf("2MB\n"); break;
		case 2: printf("1GB\n"); break;
		default:	printf("Resreved\n"); break;
	}

	printf("\t\t\tFetch Physical Address Valid: %d\n",!!(msrs[0]&1ULL<<52));
	printf("\t\t\ticache miss: %d\n",!!(msrs[0]&1ULL<<51));
	printf("\t\t\tInstruction Fetch Complete: %d\n",!!(msrs[0]&1ULL<<50));
	printf("\t\t\tInstruction Fetch Valid: %d\n",!!(msrs[0]&1ULL<<49));
	printf("\t\t\tInstruction Fetch Enabled: %d\n",!!(msrs[0]&1ULL<<48));
	printf("\t\t\tInstruction Fetch Latency: %lld\n",((msrs[0]>>32)&0xffff));
	printf("\t\t\tInstruction Fetch Count: %lld\n",((msrs[0]>>16)&0xffff)<<4);
	printf("\t\t\tInstruction Fetch Max Count: %lld\n",(msrs[0]&0xffff)<<4);

	printf("\t\tMSR IBS_FETCH_LINEAR_ADDRESS %llx\n",msrs[1]);
	printf("\t\tMSR IBS_FETCH_PHYSICAL_ADDRESS %llx\n",msrs[2]);
	if (size>24) {
		printf("\t\tMSR IBS_BRTARGET %llx\n",msrs[3]);
	}
	return 0;
}

static int dump_raw_ibs_op(unsigned char *data, int size) {

	unsigned long long *msrs;
	unsigned int *leftover;

	msrs=(unsigned long long *)(data+4);
	leftover=(unsigned int *)(data);

	printf("\t\tHeader: %x\n",leftover[0]);
	printf("\t\tMSR IBS_EXECUTION_CONTROL %llx\n",msrs[0]);
	printf("\t\t\tIbsOpCurCnt: %lld\n",((msrs[0]>>32)&0x3ffffff));
	printf("\t\t\tIBS OpCntCtl: %d\n",!!(msrs[0]&1ULL<<19));
	printf("\t\t\tIBS OpVal: %d\n",!!(msrs[0]&1ULL<<18));
	printf("\t\t\tIBS OpEn: %d\n",!!(msrs[0]&1ULL<<17));
	printf("\t\t\tIbsOpMaxCnt: %lld\n",((msrs[0]&0xffff)<<4) |
				(msrs[0]&0x3f00000));

	printf("\t\tMSR IBS_OP_LOGICAL_ADDRESS %llx\n",msrs[1]);

	printf("\t\tMSR IBS_OP_DATA %llx\n",msrs[2]);
	printf("\t\t\tRIP Invalid: %d\n",!!(msrs[2]&1ULL<<38));
	printf("\t\t\tBranch Retired: %d\n",!!(msrs[2]&1ULL<<37));
	printf("\t\t\tBranch Mispredicted: %d\n",!!(msrs[2]&1ULL<<36));
	printf("\t\t\tBranch Taken: %d\n",!!(msrs[2]&1ULL<<35));
	printf("\t\t\tReturn uop: %d\n",!!(msrs[2]&1ULL<<34));
	printf("\t\t\tMispredicted Return uop: %d\n",!!(msrs[2]&1ULL<<33));
	printf("\t\t\tTag to Retire Cycles: %lld\n",(msrs[2]>>16)&0xffff);
	printf("\t\t\tCompletion to Retire Cycles: %lld\n",msrs[2]&0xffff);

	printf("\t\tMSR IBS_OP_DATA2 (Northbridge) %llx\n",msrs[3]);
	printf("\t\t\tCache Hit State: %c\n",(msrs[3]&1ULL<<5)?'O':'M');
	printf("\t\t\tRequest destination node: %s\n",
		(msrs[3]&1ULL<<4)?"Same":"Different");
	printf("\t\t\tNorthbridge data source: ");
	switch(msrs[3]&0x7) {
		case 0:	printf("No valid status\n"); break;
		case 1: printf("L3\n"); break;
		case 2: printf("Cache from another compute unit\n"); break;
		case 3: printf("DRAM\n"); break;
		case 4: printf("Reserved remote cache\n"); break;
		case 5: printf("Reserved\n"); break;
		case 6: printf("Reserved\n"); break;
		case 7: printf("Other: MMIO/config/PCI/APIC\n"); break;
	}

	printf("\t\tMSR IBS_OP_DATA3 (cache) %llx\n",msrs[4]);
	printf("\t\t\tData Cache Miss Latency: %lld\n",
		(msrs[4]>>32)&0xffff);
	printf("\t\t\tL2TLB data hit in 1GB page: %d\n",
		!!(msrs[4]&1ULL<<19));
	printf("\t\t\tData cache physical addr valid: %d\n",
		!!(msrs[4]&1ULL<<18));
	printf("\t\t\tData cache linear addr valid: %d\n",
		!!(msrs[4]&1ULL<<17));
	printf("\t\t\tMAB hit: %d\n",
		!!(msrs[4]&1ULL<<16));
	printf("\t\t\tData cache locked operation: %d\n",
		!!(msrs[4]&1ULL<<15));
	printf("\t\t\tUncachable memory operation: %d\n",
		!!(msrs[4]&1ULL<<14));
	printf("\t\t\tWrite-combining memory operation: %d\n",
		!!(msrs[4]&1ULL<<13));
	printf("\t\t\tData forwarding store to load canceled: %d\n",
		!!(msrs[4]&1ULL<<12));
	printf("\t\t\tData forwarding store to load operation: %d\n",
		!!(msrs[4]&1ULL<<11));
	printf("\t\t\tBank conflict on load operation: %d\n",
		!!(msrs[4]&1ULL<<9));
	printf("\t\t\tMisaligned access: %d\n",
		!!(msrs[4]&1ULL<<8));
	printf("\t\t\tData cache miss: %d\n",
		!!(msrs[4]&1ULL<<7));
	printf("\t\t\tData cache L2TLB hit in 2M: %d\n",
		!!(msrs[4]&1ULL<<6));
	printf("\t\t\tData cache L2TLB hit in 1G: %d\n",
		!!(msrs[4]&1ULL<<5));
	printf("\t\t\tData cache L1TLB hit in 2M: %d\n",
		!!(msrs[4]&1ULL<<4));
	printf("\t\t\tData cache L2TLB miss: %d\n",
		!!(msrs[4]&1ULL<<3));
	printf("\t\t\tData cache L1TLB miss: %d\n",
		!!(msrs[4]&1ULL<<2));
	printf("\t\t\tOperation is a store: %d\n",
		!!(msrs[4]&1ULL<<1));
	printf("\t\t\tOperation is a load: %d\n",
		!!(msrs[4]&1ULL<<0));

	if (msrs[4]&1ULL<<17) {
		printf("\t\tMSR IBS_DC_LINEAR_ADDRESS %llx\n",msrs[5]);
	}
	if (msrs[4]&1ULL<<18) {
		printf("\t\tMSR IBS_DC_PHYSICAL_ADDRESS %llx\n",msrs[6]);
	}

	if (size>64) {
		printf("\t\tMSR IBS_OP_DATA4 %llx\n",msrs[7]);
	}
	return 0;
}



static int print_data_header(uint32_t type, uint16_t misc, uint16_t size) {

	/* FIXME */
	int sample_type=0;

	switch(type) {
		case PERF_RECORD_MMAP:
			printf("PERF_RECORD_MMAP");
			break;
		case PERF_RECORD_LOST:
			printf("PERF_RECORD_LOST");
			break;
		case PERF_RECORD_COMM:
			printf("PERF_RECORD_COMM");
			break;
		case PERF_RECORD_EXIT:
			printf("PERF_RECORD_EXIT");
			break;
		case PERF_RECORD_THROTTLE:
			printf("PERF_RECORD_THROTTLE");
			break;
		case PERF_RECORD_UNTHROTTLE:
			printf("PERF_RECORD_UNTHROTTLE");
			break;
		case PERF_RECORD_FORK:
			printf("PERF_RECORD_FORK");
			break;
		case PERF_RECORD_READ:
			printf("PERF_RECORD_READ");
			break;
		case PERF_RECORD_SAMPLE:
			printf("PERF_RECORD_SAMPLE [%x]",sample_type);
			break;
		case PERF_RECORD_MMAP2:
			printf("PERF_RECORD_MMAP2");
			break;
		case PERF_RECORD_AUX:
			printf("PERF_RECORD_AUX");
			break;
		case PERF_RECORD_ITRACE_START:
			printf("PERF_RECORD_ITRACE_START");
			break;
		case PERF_RECORD_LOST_SAMPLES:
			printf("PERF_RECORD_LOST_SAMPLES");
			break;
		case PERF_RECORD_SWITCH:
			printf("PERF_RECORD_SWITCH");
			break;
		case PERF_RECORD_SWITCH_CPU_WIDE:
			printf("PERF_RECORD_SWITCH_CPU_WIDE");
			break;
		case PERF_RECORD_NAMESPACES:
			printf("PERF_RECORD_NAMESPACES");
			break;
		case PERF_RECORD_KSYMBOL:
			printf("PERF_RECORD_KSYMBOL");
			break;
		case PERF_RECORD_BPF_EVENT:
			printf("PERF_RECORD_BPF_EVENT");
			break;

		/* "synthesized" fake events provided by perf tool */
#define PERF_RECORD_HEADER_ATTR		64
#define PERF_RECORD_HEADER_EVENT_TYPE	65 /* deprecated */
#define PERF_RECORD_HEADER_TRACING_DATA	66
#define PERF_RECORD_HEADER_BUILD_ID	67
#define PERF_RECORD_FINISHED_ROUND	68
#define PERF_RECORD_ID_INDEX		69
#define PERF_RECORD_AUXTRACE_INFO	70
#define PERF_RECORD_AUXTRACE		71
#define PERF_RECORD_AUXTRACE_ERROR	72
#define	PERF_RECORD_THREAD_MAP		73
#define	PERF_RECORD_CPU_MAP		74
#define	PERF_RECORD_STAT_CONFIG		75
#define	PERF_RECORD_STAT		76
#define	PERF_RECORD_STAT_ROUND		77
#define	PERF_RECORD_EVENT_UPDATE	78
#define	PERF_RECORD_TIME_CONV		79
#define PERF_RECORD_HEADER_FEATURE	80
#define PERF_RECORD_COMPRESSED		81

		case PERF_RECORD_HEADER_ATTR:
			printf("PERF_RECORD_HEADER_ATTR");
			break;
		case PERF_RECORD_HEADER_EVENT_TYPE:
			printf("PERF_RECORD_HEADER_EVENT_TYPE");
			break;
		case PERF_RECORD_HEADER_TRACING_DATA:
			printf("PERF_RECORD_HEADER_TRACING_DATA");
			break;
		case PERF_RECORD_HEADER_BUILD_ID:
			printf("PERF_RECORD_HEADER_BUILD_ID");
			break;
		case PERF_RECORD_FINISHED_ROUND:
			printf("PERF_RECORD_FINISHED_ROUND");
			break;
		case PERF_RECORD_ID_INDEX:
			printf("PERF_RECORD_ID_INDEX");
			break;
		case PERF_RECORD_AUXTRACE_INFO:
			printf("PERF_RECORD_AUXTRACE_INFO");
			break;
		case PERF_RECORD_AUXTRACE:
			printf("PERF_RECORD_AUXTRACE");
			break;
		case PERF_RECORD_AUXTRACE_ERROR:
			printf("PERF_RECORD_AUXTRACE_ERROR");
			break;
		case PERF_RECORD_THREAD_MAP:
			printf("PERF_RECORD_THREAD_MAP");
			break;
		case PERF_RECORD_CPU_MAP:
			printf("PERF_RECORD_CPU_MAP");
			break;
		case PERF_RECORD_STAT_CONFIG:
			printf("PERF_RECORD_STAT_CONFIG");
			break;
		case PERF_RECORD_STAT:
			printf("PERF_RECORD_STAT");
			break;
		case PERF_RECORD_STAT_ROUND:
			printf("PERF_RECORD_STAT_ROUND");
			break;
		case PERF_RECORD_EVENT_UPDATE:
			printf("PERF_RECORD_EVENT_UPDATE");
			break;
		case PERF_RECORD_TIME_CONV:
			printf("PERF_RECORD_TIME_CONV");
			break;
		case PERF_RECORD_HEADER_FEATURE:
			printf("PERF_RECORD_HEADER_FEATURE");
			break;
		case PERF_RECORD_COMPRESSED:
			printf("PERF_RECORD_COMPRESSED");
			break;


		default: printf("UNKNOWN %d",type);
			break;
	}

	printf(", MISC=%d (",misc);
	switch(misc & PERF_RECORD_MISC_CPUMODE_MASK) {
		case PERF_RECORD_MISC_CPUMODE_UNKNOWN:
			printf("PERF_RECORD_MISC_CPUMODE_UNKNOWN");
			break;
		case PERF_RECORD_MISC_KERNEL:
			printf("PERF_RECORD_MISC_KERNEL");
			break;
		case PERF_RECORD_MISC_USER:
			printf("PERF_RECORD_MISC_USER");
			break;
		case PERF_RECORD_MISC_HYPERVISOR:
			printf("PERF_RECORD_MISC_HYPERVISOR");
			break;
		case PERF_RECORD_MISC_GUEST_KERNEL:
			printf("PERF_RECORD_MISC_GUEST_KERNEL");
			break;
		case PERF_RECORD_MISC_GUEST_USER:
			printf("PERF_RECORD_MISC_GUEST_USER");
			break;
		default:
			printf("Unknown %d!\n",misc);
			break;
	}

	/* All three have the same value */
	if (misc & PERF_RECORD_MISC_MMAP_DATA) {
		if (type==PERF_RECORD_MMAP) {
			printf(",PERF_RECORD_MISC_MMAP_DATA ");
		}
		else if (type==PERF_RECORD_COMM) {
			printf(",PERF_RECORD_MISC_COMM_EXEC ");
		}
		else if ((type==PERF_RECORD_SWITCH) ||
				(type==PERF_RECORD_SWITCH_CPU_WIDE)) {
			printf(",PERF_RECORD_MISC_SWITCH_OUT ");
		}
		else {
			printf("UNKNOWN ALIAS!!! ");
		}
	}


	if (misc & PERF_RECORD_MISC_EXACT_IP) {
		printf(",PERF_RECORD_MISC_EXACT_IP ");
	}

	if (misc & PERF_RECORD_MISC_EXT_RESERVED) {
		printf(",PERF_RECORD_MISC_EXT_RESERVED ");
	}

	printf("), Size=%d\n",size);

	return 0;
}

int parse_perf_record(unsigned char *data) {

	int offset=0,i;
	int sample_type=0,read_format=0,raw_type=0,reg_mask=0;

	uint32_t record_type;
	uint16_t record_misc;
	uint16_t record_size;

	/********************/
	/* Print event Type */
	/********************/

	record_type=get_uint32(data,offset);
        offset+=4;
	record_misc=get_uint16(data,offset);
        offset+=2;
	record_size=get_uint16(data,offset);
        offset+=2;

	print_data_header(record_type, record_misc, record_size);


	/***********************/
	/* Print event Details */
	/***********************/

	switch(record_type) {

		/* Lost */
		case PERF_RECORD_LOST: {
			long long id,lost;
			memcpy(&id,&data[offset],sizeof(long long));
			printf("\tID: %lld\n",id);
			offset+=8;
			memcpy(&lost,&data[offset],sizeof(long long));
			printf("\tLOST: %lld\n",lost);
			offset+=8;
			}
			break;

		/* COMM */
		case PERF_RECORD_COMM: {
			int pid,tid,string_size;
			char *string;

			memcpy(&pid,&data[offset],sizeof(int));
			printf("\tPID: %d\n",pid);
			offset+=4;
			memcpy(&tid,&data[offset],sizeof(int));
			printf("\tTID: %d\n",tid);
			offset+=4;

			/* FIXME: sample_id handling? */

			/* two ints plus the 64-bit header */
			string_size=record_size-16;
			string=calloc(string_size,sizeof(char));
			memcpy(string,&data[offset],string_size);
			printf("\tcomm: %s\n",string);
			offset+=string_size;
			if (string) free(string);
			}
			break;

		/* Fork */
		case PERF_RECORD_FORK: {
			int pid,ppid,tid,ptid;
			long long fork_time;

			memcpy(&pid,&data[offset],sizeof(int));
			printf("\tPID: %d\n",pid);
			offset+=4;
			memcpy(&ppid,&data[offset],sizeof(int));
			printf("\tPPID: %d\n",ppid);
			offset+=4;
			memcpy(&tid,&data[offset],sizeof(int));
			printf("\tTID: %d\n",tid);
			offset+=4;
			memcpy(&ptid,&data[offset],sizeof(int));
			printf("\tPTID: %d\n",ptid);
			offset+=4;
			memcpy(&fork_time,&data[offset],sizeof(long long));
			printf("\tTime: %lld\n",fork_time);
			offset+=8;
			}
			break;

		/* mmap */
		case PERF_RECORD_MMAP: {
			int pid,tid,string_size;
			long long address,len,pgoff;
			char *filename;

			memcpy(&pid,&data[offset],sizeof(int));
			printf("\tPID: %d\n",pid);
			offset+=4;
			memcpy(&tid,&data[offset],sizeof(int));
			printf("\tTID: %d\n",tid);
			offset+=4;
			memcpy(&address,&data[offset],sizeof(long long));
			printf("\tAddress: %llx\n",address);
			offset+=8;
			memcpy(&len,&data[offset],sizeof(long long));
			printf("\tLength: %llx\n",len);
			offset+=8;
			memcpy(&pgoff,&data[offset],sizeof(long long));
			printf("\tPage Offset: %llx\n",pgoff);
			offset+=8;

			string_size=record_size-40;
			filename=calloc(string_size,sizeof(char));
			memcpy(filename,&data[offset],string_size);
			printf("\tFilename: %s\n",filename);
			offset+=string_size;
			if (filename) free(filename);

			}
			break;

		/* mmap2 */
		case PERF_RECORD_MMAP2: {
			int pid,tid,string_size;
			long long address,len,pgoff;
			int major,minor;
			long long ino,ino_generation;
			int prot,flags;
			char *filename;

			memcpy(&pid,&data[offset],sizeof(int));
			printf("\tPID: %d\n",pid);
			offset+=4;
			memcpy(&tid,&data[offset],sizeof(int));
			printf("\tTID: %d\n",tid);
			offset+=4;
			memcpy(&address,&data[offset],sizeof(long long));
			printf("\tAddress: %llx\n",address);
			offset+=8;
			memcpy(&len,&data[offset],sizeof(long long));
			printf("\tLength: %llx\n",len);
			offset+=8;
			memcpy(&pgoff,&data[offset],sizeof(long long));
			printf("\tPage Offset: %llx\n",pgoff);
			offset+=8;
			memcpy(&major,&data[offset],sizeof(int));
			printf("\tMajor: %d\n",major);
			offset+=4;
			memcpy(&minor,&data[offset],sizeof(int));
			printf("\tMinor: %d\n",minor);
			offset+=4;
			memcpy(&ino,&data[offset],sizeof(long long));
			printf("\tIno: %llx\n",ino);
			offset+=8;
			memcpy(&ino_generation,&data[offset],sizeof(long long));
			printf("\tIno generation: %llx\n",ino_generation);
			offset+=8;
			memcpy(&prot,&data[offset],sizeof(int));
			printf("\tProt: %d\n",prot);
			offset+=4;
			memcpy(&flags,&data[offset],sizeof(int));
			printf("\tFlags: %d\n",flags);
			offset+=4;

			string_size=record_size-72;
			filename=calloc(string_size,sizeof(char));
			memcpy(filename,&data[offset],string_size);
			printf("\tFilename: %s\n",filename);
			offset+=string_size;
			if (filename) free(filename);

			}
			break;

		/* Exit */
		case PERF_RECORD_EXIT: {
			int pid,ppid,tid,ptid;
			long long fork_time;

			memcpy(&pid,&data[offset],sizeof(int));
			printf("\tPID: %d\n",pid);
			offset+=4;
			memcpy(&ppid,&data[offset],sizeof(int));
			printf("\tPPID: %d\n",ppid);
			offset+=4;
			memcpy(&tid,&data[offset],sizeof(int));
			printf("\tTID: %d\n",tid);
			offset+=4;
			memcpy(&ptid,&data[offset],sizeof(int));
			printf("\tPTID: %d\n",ptid);
			offset+=4;
			memcpy(&fork_time,&data[offset],sizeof(long long));
			printf("\tTime: %lld\n",fork_time);
			offset+=8;
			}
			break;

		/* Throttle/Unthrottle */
		case PERF_RECORD_THROTTLE:
		case PERF_RECORD_UNTHROTTLE: {
			long long throttle_time,id,stream_id;

			memcpy(&throttle_time,&data[offset],sizeof(long long));
			printf("\tTime: %lld\n",throttle_time);
			offset+=8;
			memcpy(&id,&data[offset],sizeof(long long));
			printf("\tID: %lld\n",id);
			offset+=8;
			memcpy(&stream_id,&data[offset],sizeof(long long));
			printf("\tStream ID: %lld\n",stream_id);
			offset+=8;

			}
			break;

		/* Sample */
		case PERF_RECORD_SAMPLE:
			if (sample_type & PERF_SAMPLE_IP) {
				long long ip;
				memcpy(&ip,&data[offset],sizeof(long long));
				printf("\tPERF_SAMPLE_IP, IP: %llx\n",ip);
				offset+=8;
			}
			if (sample_type & PERF_SAMPLE_TID) {
				int pid, tid;
				memcpy(&pid,&data[offset],sizeof(int));
				memcpy(&tid,&data[offset+4],sizeof(int));
				printf("\tPERF_SAMPLE_TID, pid: %d  tid %d\n",pid,tid);
				offset+=8;
			}

			if (sample_type & PERF_SAMPLE_TIME) {
				long long time;
				memcpy(&time,&data[offset],sizeof(long long));
				printf("\tPERF_SAMPLE_TIME, time: %lld\n",time);
				offset+=8;
			}
			if (sample_type & PERF_SAMPLE_ADDR) {
				long long addr;
				memcpy(&addr,&data[offset],sizeof(long long));
				printf("\tPERF_SAMPLE_ADDR, addr: %llx\n",addr);
				offset+=8;
			}
			if (sample_type & PERF_SAMPLE_ID) {
				long long sample_id;
				memcpy(&sample_id,&data[offset],sizeof(long long));
				printf("\tPERF_SAMPLE_ID, sample_id: %lld\n",sample_id);
				offset+=8;
			}
			if (sample_type & PERF_SAMPLE_STREAM_ID) {
				long long sample_stream_id;
				memcpy(&sample_stream_id,&data[offset],sizeof(long long));
				printf("\tPERF_SAMPLE_STREAM_ID, sample_stream_id: %lld\n",sample_stream_id);
				offset+=8;
			}
			if (sample_type & PERF_SAMPLE_CPU) {
				int cpu, res;
				memcpy(&cpu,&data[offset],sizeof(int));
				memcpy(&res,&data[offset+4],sizeof(int));
				printf("\tPERF_SAMPLE_CPU, cpu: %d  res %d\n",cpu,res);
				offset+=8;
			}
			if (sample_type & PERF_SAMPLE_PERIOD) {
				long long period;
				memcpy(&period,&data[offset],sizeof(long long));
				printf("\tPERF_SAMPLE_PERIOD, period: %lld\n",period);
				offset+=8;
			}
			if (sample_type & PERF_SAMPLE_READ) {
				int length;

				printf("\tPERF_SAMPLE_READ, read_format\n");
				length=handle_struct_read_format(&data[offset],
						read_format);
				if (length>=0) offset+=length;
			}
			if (sample_type & PERF_SAMPLE_CALLCHAIN) {
				long long nr,ip;
				memcpy(&nr,&data[offset],sizeof(long long));
				printf("\tPERF_SAMPLE_CALLCHAIN, callchain length: %lld\n",nr);
				offset+=8;

				for(i=0;i<nr;i++) {
					memcpy(&ip,&data[offset],sizeof(long long));
					printf("\t\t ip[%d]: %llx\n",i,ip);
					offset+=8;
				}
			}
			if (sample_type & PERF_SAMPLE_RAW) {
				int size;

				memcpy(&size,&data[offset],sizeof(int));
				printf("\tPERF_SAMPLE_RAW, Raw length: %d\n",size);
				offset+=4;

				if (raw_type==RAW_IBS_FETCH) {
					dump_raw_ibs_fetch(&data[offset],size);
				}
				else if (raw_type==RAW_IBS_OP) {
					dump_raw_ibs_op(&data[offset],size);
				}
				else {
					printf("\t\t");
					for(i=0;i<size;i++) {
						printf("%d ",data[offset+i]);
					}
					printf("\n");
				}
				offset+=size;
			}

			if (sample_type & PERF_SAMPLE_BRANCH_STACK) {
				long long bnr;
				memcpy(&bnr,&data[offset],sizeof(long long));
				printf("\tPERF_SAMPLE_BRANCH_STACK, branch_stack entries: %lld\n",bnr);
				offset+=8;

				for(i=0;i<bnr;i++) {
					long long from,to,flags;

					/* From value */
					memcpy(&from,&data[offset],sizeof(long long));
					offset+=8;

					/* To Value */
					memcpy(&to,&data[offset],sizeof(long long));
					offset+=8;
					printf("\t\t lbr[%d]: %llx %llx ",
							i,from,to);

					/* Flags */
					memcpy(&flags,&data[offset],sizeof(long long));
					offset+=8;

					if (flags==0) printf("0");

					if (flags&1) {
						printf("MISPREDICTED ");
						flags&=~2;
					}
					if (flags&2) {
						printf("PREDICTED ");
						flags&=~2;
					}

					if (flags&4) {
						printf("IN_TRANSACTION ");
						flags&=~4;
					}

					if (flags&8) {
						printf("TRANSACTION_ABORT ");
						flags&=~8;
					}
					printf("\n");
	      			}
	   		}

			if (sample_type & PERF_SAMPLE_REGS_USER) {
				long long abi;

				memcpy(&abi,&data[offset],sizeof(long long));
				printf("\tPERF_SAMPLE_REGS_USER, ABI: ");
				if (abi==PERF_SAMPLE_REGS_ABI_NONE) printf ("PERF_SAMPLE_REGS_ABI_NONE");
				if (abi==PERF_SAMPLE_REGS_ABI_32) printf("PERF_SAMPLE_REGS_ABI_32");
				if (abi==PERF_SAMPLE_REGS_ABI_64) printf("PERF_SAMPLE_REGS_ABI_64");
				printf("\n");
				offset+=8;

				offset+=print_regs(abi,reg_mask,
						&data[offset]);

				printf("\n");
			}

			if (sample_type & PERF_SAMPLE_REGS_INTR) {
				long long abi;

				memcpy(&abi,&data[offset],sizeof(long long));
				printf("\tPERF_SAMPLE_REGS_INTR, ABI: ");
				if (abi==PERF_SAMPLE_REGS_ABI_NONE) printf ("PERF_SAMPLE_REGS_ABI_NONE");
				if (abi==PERF_SAMPLE_REGS_ABI_32) printf("PERF_SAMPLE_REGS_ABI_32");
				if (abi==PERF_SAMPLE_REGS_ABI_64) printf("PERF_SAMPLE_REGS_ABI_64");
				printf("\n");
				offset+=8;

				offset+=print_regs(abi,reg_mask,
						&data[offset]);

				printf("\n");
			}

			if (sample_type & PERF_SAMPLE_STACK_USER) {
				long long size,dyn_size;
				int *stack_data;
				int k;

				memcpy(&size,&data[offset],sizeof(long long));
				printf("\tPERF_SAMPLE_STACK_USER, Requested size: %lld\n",size);
				offset+=8;

				stack_data=malloc(size);
				memcpy(stack_data,&data[offset],size);
				offset+=size;

				memcpy(&dyn_size,&data[offset],sizeof(long long));
				printf("\t\tDynamic (used) size: %lld\n",dyn_size);
				offset+=8;

				printf("\t\t");
				for(k=0;k<dyn_size;k+=4) {
					printf("0x%x ",stack_data[k]);
				}

				free(stack_data);

				printf("\n");
			}

			if (sample_type & PERF_SAMPLE_WEIGHT) {
				long long weight;

				memcpy(&weight,&data[offset],sizeof(long long));
				printf("\tPERF_SAMPLE_WEIGHT, Weight: %lld ",weight);
				offset+=8;

				printf("\n");
			}

			if (sample_type & PERF_SAMPLE_DATA_SRC) {
				long long src;

				memcpy(&src,&data[offset],sizeof(long long));
				printf("\tPERF_SAMPLE_DATA_SRC, Raw: %llx\n",src);
				offset+=8;

				if (src!=0) printf("\t\t");
				if (src & (PERF_MEM_OP_NA<<PERF_MEM_OP_SHIFT))
					printf("Op Not available ");
				if (src & (PERF_MEM_OP_LOAD<<PERF_MEM_OP_SHIFT))
					printf("Load ");
				if (src & (PERF_MEM_OP_STORE<<PERF_MEM_OP_SHIFT))
					printf("Store ");
				if (src & (PERF_MEM_OP_PFETCH<<PERF_MEM_OP_SHIFT))
					printf("Prefetch ");
				if (src & (PERF_MEM_OP_EXEC<<PERF_MEM_OP_SHIFT))
					printf("Executable code ");

				if (src & (PERF_MEM_LVL_NA<<PERF_MEM_LVL_SHIFT))
					printf("Level Not available ");
				if (src & (PERF_MEM_LVL_HIT<<PERF_MEM_LVL_SHIFT))
					printf("Hit ");
				if (src & (PERF_MEM_LVL_MISS<<PERF_MEM_LVL_SHIFT))
					printf("Miss ");
				if (src & (PERF_MEM_LVL_L1<<PERF_MEM_LVL_SHIFT))
					printf("L1 cache ");
				if (src & (PERF_MEM_LVL_LFB<<PERF_MEM_LVL_SHIFT))
					printf("Line fill buffer ");
				if (src & (PERF_MEM_LVL_L2<<PERF_MEM_LVL_SHIFT))
					printf("L2 cache ");
				if (src & (PERF_MEM_LVL_L3<<PERF_MEM_LVL_SHIFT))
					printf("L3 cache ");
				if (src & (PERF_MEM_LVL_LOC_RAM<<PERF_MEM_LVL_SHIFT))
					printf("Local DRAM ");
				if (src & (PERF_MEM_LVL_REM_RAM1<<PERF_MEM_LVL_SHIFT))
					printf("Remote DRAM 1 hop ");
				if (src & (PERF_MEM_LVL_REM_RAM2<<PERF_MEM_LVL_SHIFT))
					printf("Remote DRAM 2 hops ");
				if (src & (PERF_MEM_LVL_REM_CCE1<<PERF_MEM_LVL_SHIFT))
					printf("Remote cache 1 hop ");
				if (src & (PERF_MEM_LVL_REM_CCE2<<PERF_MEM_LVL_SHIFT))
					printf("Remote cache 2 hops ");
				if (src & (PERF_MEM_LVL_IO<<PERF_MEM_LVL_SHIFT))
					printf("I/O memory ");
				if (src & (PERF_MEM_LVL_UNC<<PERF_MEM_LVL_SHIFT))
					printf("Uncached memory ");

				if (src & (PERF_MEM_SNOOP_NA<<PERF_MEM_SNOOP_SHIFT))
					printf("Not available ");
				if (src & (PERF_MEM_SNOOP_NONE<<PERF_MEM_SNOOP_SHIFT))
					printf("No snoop ");
				if (src & (PERF_MEM_SNOOP_HIT<<PERF_MEM_SNOOP_SHIFT))
					printf("Snoop hit ");
				if (src & (PERF_MEM_SNOOP_MISS<<PERF_MEM_SNOOP_SHIFT))
					printf("Snoop miss ");
				if (src & (PERF_MEM_SNOOP_HITM<<PERF_MEM_SNOOP_SHIFT))
					printf("Snoop hit modified ");

				if (src & (PERF_MEM_LOCK_NA<<PERF_MEM_LOCK_SHIFT))
					printf("Not available ");
				if (src & (PERF_MEM_LOCK_LOCKED<<PERF_MEM_LOCK_SHIFT))
					printf("Locked transaction ");

				if (src & (PERF_MEM_TLB_NA<<PERF_MEM_TLB_SHIFT))
					printf("Not available ");
				if (src & (PERF_MEM_TLB_HIT<<PERF_MEM_TLB_SHIFT))
					printf("Hit ");
				if (src & (PERF_MEM_TLB_MISS<<PERF_MEM_TLB_SHIFT))
					printf("Miss ");
				if (src & (PERF_MEM_TLB_L1<<PERF_MEM_TLB_SHIFT))
					printf("Level 1 TLB ");
				if (src & (PERF_MEM_TLB_L2<<PERF_MEM_TLB_SHIFT))
					printf("Level 2 TLB ");
				if (src & (PERF_MEM_TLB_WK<<PERF_MEM_TLB_SHIFT))
					printf("Hardware walker ");
				if (src & ((long long)PERF_MEM_TLB_OS<<PERF_MEM_TLB_SHIFT))
					printf("OS fault handler ");

				printf("\n");
			}

			if (sample_type & PERF_SAMPLE_IDENTIFIER) {
				long long abi;

				memcpy(&abi,&data[offset],sizeof(long long));
				printf("\tPERF_SAMPLE_IDENTIFIER, Raw length: %lld\n",abi);
				offset+=8;

				printf("\n");
			}

			if (sample_type & PERF_SAMPLE_TRANSACTION) {
				long long abi;

				memcpy(&abi,&data[offset],sizeof(long long));
				printf("\tPERF_SAMPLE_TRANSACTION, Raw length: %lld\n",abi);
				offset+=8;

				printf("\n");
			}
			break;

		/* AUX */
		case PERF_RECORD_AUX: {
			long long aux_offset,aux_size,flags;
			long long sample_id;

			memcpy(&aux_offset,&data[offset],sizeof(long long));
			printf("\tAUX_OFFSET: %lld\n",aux_offset);
			offset+=8;

			memcpy(&aux_size,&data[offset],sizeof(long long));
			printf("\tAUX_SIZE: %lld\n",aux_size);
			offset+=8;

			memcpy(&flags,&data[offset],sizeof(long long));
			printf("\tFLAGS: %llx ",flags);
			if (flags & PERF_AUX_FLAG_TRUNCATED) {
				printf("FLAG_TRUNCATED ");
			}
			if (flags & PERF_AUX_FLAG_OVERWRITE) {
				printf("FLAG_OVERWRITE ");
			}
			printf("\n");

			offset+=8;

			memcpy(&sample_id,&data[offset],sizeof(long long));
			printf("\tSAMPLE_ID: %lld\n",sample_id);
			offset+=8;

			}
			break;

		/* itrace start */
		case PERF_RECORD_ITRACE_START: {
			int pid,tid;

			memcpy(&pid,&data[offset],sizeof(int));
			printf("\tPID: %d\n",pid);
			offset+=4;

			memcpy(&tid,&data[offset],sizeof(int));
			printf("\tTID: %d\n",tid);
			offset+=4;
			}
			break;

		/* lost samples PEBS */
		case PERF_RECORD_LOST_SAMPLES: {
			long long lost,sample_id;

			memcpy(&lost,&data[offset],sizeof(long long));
			printf("\tLOST: %lld\n",lost);
			offset+=8;

			memcpy(&sample_id,&data[offset],sizeof(long long));
			printf("\tSAMPLE_ID: %lld\n",sample_id);
			offset+=8;
			}
			break;

		/* context switch */
		case PERF_RECORD_SWITCH: {
			long long sample_id;

			memcpy(&sample_id,&data[offset],sizeof(long long));
			printf("\tSAMPLE_ID: %lld\n",sample_id);
			offset+=8;
			}
			break;

		/* context switch cpu-wide*/
		case PERF_RECORD_SWITCH_CPU_WIDE: {
			int prev_pid,prev_tid;
			long long sample_id;

			memcpy(&prev_pid,&data[offset],sizeof(int));
			printf("\tPREV_PID: %d\n",prev_pid);
			offset+=4;

			memcpy(&prev_tid,&data[offset],sizeof(int));
			printf("\tPREV_TID: %d\n",prev_tid);
			offset+=4;

			memcpy(&sample_id,&data[offset],sizeof(long long));
			printf("\tSAMPLE_ID: %lld\n",sample_id);
			offset+=8;
			}
			break;



/* Synthesized by perf tool */

		case PERF_RECORD_HEADER_ATTR:
		case PERF_RECORD_HEADER_EVENT_TYPE:	/* deprecated */
		case PERF_RECORD_HEADER_TRACING_DATA:
		case PERF_RECORD_HEADER_BUILD_ID:
			printf("\tUnknown type %d\n",record_type);
			/* Probably best to just skip it all */
			offset=record_size;
			break;

		/*
		struct stat_round_event {
        		struct perf_event_header        header;
			u64                             type;
			u64                             time;
		};
		*/
		case PERF_RECORD_FINISHED_ROUND: {
				uint64_t type,time;

				type=get_uint64(data,offset);
				offset+=8;
				time=get_uint64(data,offset);
				offset+=8;
				printf("\tType: %ld\n",type);
				printf("\tTime: %ld\n",time);
			}
			break;
		case PERF_RECORD_ID_INDEX:
		case PERF_RECORD_AUXTRACE_INFO:
		case PERF_RECORD_AUXTRACE:
		case PERF_RECORD_AUXTRACE_ERROR:
		case PERF_RECORD_THREAD_MAP:
		case PERF_RECORD_CPU_MAP:
		case PERF_RECORD_STAT_CONFIG:
		case PERF_RECORD_STAT:
		case PERF_RECORD_STAT_ROUND:
		case PERF_RECORD_EVENT_UPDATE:
			printf("\tUnknown type %d\n",record_type);
			/* Probably best to just skip it all */
			offset=record_size;
			break;

		/*
		struct time_conv_event {
        		struct perf_event_header header;
        		u64 time_shift;
       			 u64 time_mult;
        		u64 time_zero;
		};
		*/

		case PERF_RECORD_TIME_CONV: {
				uint64_t time_shift,time_mult,time_zero;

				time_shift=get_uint64(data,offset);
				offset+=8;
				time_mult=get_uint64(data,offset);
				offset+=8;
				time_zero=get_uint64(data,offset);
				offset+=8;
				printf("\tTime shift: %ld\n",time_shift);
				printf("\tTime mult: %ld\n",time_mult);
				printf("\tTime zero: %ld\n",time_zero);
			}
			break;

		case PERF_RECORD_HEADER_FEATURE:
		case PERF_RECORD_COMPRESSED:

		default:
			printf("\tUnknown type %d\n",record_type);
			/* Probably best to just skip it all */
			offset=record_size;

	}

	return record_size;
}
