/* Read the RAPL registers on a sandybridge-ep machine */
/* Code based on Intel RAPL driver by Zhang Rui <rui.zhang@intel.com> */
/* */
/* The /dev/cpu/??/msr driver must be enabled and permissions set */
/* to allow read access for this to work. */
/* */
/* Code to properly get this info from Linux through a real device */
/* driver and the perf tool should be available as of Linux 3.14 */
/* Compile with: gcc -O2 -Wall -o rapl-read rapl-read.c -lm */
/* */
/* Vince Weaver -- vincent.weaver @ maine.edu -- 29 November 2013 */
/* */
/* Additional contributions by: */
/* Romain Dolbeau -- romain @ dolbeau.org */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include <math.h>
#include <string.h>

#include <sys/syscall.h>
#include <linux/perf_event.h>

#define MSR_RAPL_POWER_UNIT 0x606

/*
* Platform specific RAPL Domains.
* Note that PP1 RAPL Domain is supported on 062A only
* And DRAM RAPL Domain is supported on 062D only
*/
/* Package RAPL Domain */
#define MSR_PKG_RAPL_POWER_LIMIT 0x610
#define MSR_PKG_ENERGY_STATUS 0x611
#define MSR_PKG_PERF_STATUS 0x613
#define MSR_PKG_POWER_INFO 0x614

/* PP0 RAPL Domain */
#define MSR_PP0_POWER_LIMIT 0x638
#define MSR_PP0_ENERGY_STATUS 0x639
#define MSR_PP0_POLICY 0x63A
#define MSR_PP0_PERF_STATUS 0x63B

/* PP1 RAPL Domain, may reflect to uncore devices */
#define MSR_PP1_POWER_LIMIT 0x640
#define MSR_PP1_ENERGY_STATUS 0x641
#define MSR_PP1_POLICY 0x642

/* DRAM RAPL Domain */
#define MSR_DRAM_POWER_LIMIT 0x618
#define MSR_DRAM_ENERGY_STATUS 0x619
#define MSR_DRAM_PERF_STATUS 0x61B
#define MSR_DRAM_POWER_INFO 0x61C

/* RAPL UNIT BITMASK */
#define POWER_UNIT_OFFSET 0
#define POWER_UNIT_MASK 0x0F

#define ENERGY_UNIT_OFFSET 0x08
#define ENERGY_UNIT_MASK 0x1F00

#define TIME_UNIT_OFFSET 0x10
#define TIME_UNIT_MASK 0xF000

static int open_msr(int core) {

  char msr_filename[BUFSIZ];
  int fd;

  sprintf(msr_filename, "/dev/cpu/%d/msr", core);
  fd = open(msr_filename, O_RDONLY);
  if ( fd < 0 ) {
    if ( errno == ENXIO ) {
      fprintf(stderr, "rdmsr: No CPU %d\n", core);
      exit(2);
    } else if ( errno == EIO ) {
      fprintf(stderr, "rdmsr: CPU %d doesn't support MSRs\n", core);
      exit(3);
    } else {
      perror("rdmsr:open");
      fprintf(stderr,"Trying to open %s\n",msr_filename);
      exit(127);
    }
  }

  return fd;
}

static long long read_msr(int fd, int which) {

  uint64_t data;

  if ( pread(fd, &data, sizeof data, which) != sizeof data ) {
    perror("rdmsr:pread");
    exit(127);
  }

  return (long long)data;
}

#define CPU_SANDYBRIDGE 42
#define CPU_SANDYBRIDGE_EP 45
#define CPU_IVYBRIDGE 58
#define CPU_IVYBRIDGE_EP 62
#define CPU_HASWELL 60

static int detect_cpu(void) {

FILE *fff;

int family,model=-1;
char buffer[BUFSIZ],*result;
char vendor[BUFSIZ];

fff=fopen("/proc/cpuinfo","r");
if (fff==NULL) return -1;

while(1) {
result=fgets(buffer,BUFSIZ,fff);
if (result==NULL) break;

if (!strncmp(result,"vendor_id",8)) {
sscanf(result,"%*s%*s%s",vendor);

if (strncmp(vendor,"GenuineIntel",12)) {
printf("%s not an Intel chip\n",vendor);
return -1;
}
}

if (!strncmp(result,"cpu family",10)) {
sscanf(result,"%*s%*s%*s%d",&family);
if (family!=6) {
printf("Wrong CPU family %d\n",family);
return -1;
}
}

if (!strncmp(result,"model",5)) {
sscanf(result,"%*s%*s%d",&model);
}

}

fclose(fff);

switch(model) {
case CPU_SANDYBRIDGE:
printf("Found Sandybridge CPU\n");
break;
case CPU_SANDYBRIDGE_EP:
printf("Found Sandybridge-EP CPU\n");
break;
case CPU_IVYBRIDGE:
printf("Found Ivybridge CPU\n");
break;
case CPU_IVYBRIDGE_EP:
printf("Found Ivybridge-EP CPU\n");
break;
case CPU_HASWELL:
printf("Found Haswell CPU\n");
break;
default:	printf("Unsupported model %d\n",model);
model=-1;
break;
}

return model;
}


/*******************************/
/* MSR code */
/*******************************/
static int rapl_msr(int core, char * command) {

  int fd;
  long long result;
  double power_units,energy_units,time_units;
  double package_before,package_after;
  double pp0_before,pp0_after;
  double pp1_before=0.0,pp1_after;
  double dram_before=0.0,dram_after;
  double thermal_spec_power,minimum_power,maximum_power,time_window;
  int cpu_model;

  cpu_model=detect_cpu();
  if (cpu_model<0) {
printf("Unsupported CPU type\n");
return -1;
  }

  printf("Checking core #%d\n",core);

  fd=open_msr(core);

  /* Calculate the units used */
  result=read_msr(fd,MSR_RAPL_POWER_UNIT);

  power_units=pow(0.5,(double)(result&0xf));
  energy_units=pow(0.5,(double)((result>>8)&0x1f));
  time_units=pow(0.5,(double)((result>>16)&0xf));

  printf("Power units = %.3fW\n",power_units);
  printf("Energy units = %.8fJ\n",energy_units);
  printf("Time units = %.8fs\n",time_units);
  printf("\n");

  /* Show package power info */
  result=read_msr(fd,MSR_PKG_POWER_INFO);
  thermal_spec_power=power_units*(double)(result&0x7fff);
  printf("Package thermal spec: %.3fW\n",thermal_spec_power);
  minimum_power=power_units*(double)((result>>16)&0x7fff);
  printf("Package minimum power: %.3fW\n",minimum_power);
  maximum_power=power_units*(double)((result>>32)&0x7fff);
  printf("Package maximum power: %.3fW\n",maximum_power);
  time_window=time_units*(double)((result>>48)&0x7fff);
  printf("Package maximum time window: %.6fs\n",time_window);

  /* Show package power limit */
  result=read_msr(fd,MSR_PKG_RAPL_POWER_LIMIT);
  printf("Package power limits are %s\n", (result >> 63) ? "locked" : "unlocked");
  double pkg_power_limit_1 = power_units*(double)((result>>0)&0x7FFF);
  double pkg_time_window_1 = time_units*(double)((result>>17)&0x007F);
  printf("Package power limit #1: %.3fW for %.6fs (%s, %s)\n", pkg_power_limit_1, pkg_time_window_1,
           (result & (1LL<<15)) ? "enabled" : "disabled",
           (result & (1LL<<16)) ? "clamped" : "not_clamped");
  double pkg_power_limit_2 = power_units*(double)((result>>32)&0x7FFF);
  double pkg_time_window_2 = time_units*(double)((result>>49)&0x007F);
  printf("Package power limit #2: %.3fW for %.6fs (%s, %s)\n", pkg_power_limit_2, pkg_time_window_2,
          (result & (1LL<<47)) ? "enabled" : "disabled",
          (result & (1LL<<48)) ? "clamped" : "not_clamped");

  printf("\n");

  /* result=read_msr(fd,MSR_RAPL_POWER_UNIT); */

  result=read_msr(fd,MSR_PKG_ENERGY_STATUS);
  package_before=(double)result*energy_units;
  printf("Package energy before: %.6fJ\n",package_before);

  /* only available on *Bridge-EP */
  if ((cpu_model==CPU_SANDYBRIDGE_EP) || (cpu_model==CPU_IVYBRIDGE_EP))
  {
    result=read_msr(fd,MSR_PKG_PERF_STATUS);
    double acc_pkg_throttled_time=(double)result*time_units;
    printf("Accumulated Package Throttled Time : %.6fs\n",acc_pkg_throttled_time);
  }

  result=read_msr(fd,MSR_PP0_ENERGY_STATUS);
  pp0_before=(double)result*energy_units;
  printf("PowerPlane0 (core) for core %d energy before: %.6fJ\n",core,pp0_before);

  result=read_msr(fd,MSR_PP0_POLICY);
  int pp0_policy=(int)result&0x001f;
  printf("PowerPlane0 (core) for core %d policy: %d\n",core,pp0_policy);

  /* only available on *Bridge-EP */
  if ((cpu_model==CPU_SANDYBRIDGE_EP) || (cpu_model==CPU_IVYBRIDGE_EP))
  {
    result=read_msr(fd,MSR_PP0_PERF_STATUS);
    double acc_pp0_throttled_time=(double)result*time_units;
    printf("PowerPlane0 (core) Accumulated Throttled Time : %.6fs\n",acc_pp0_throttled_time);
  }

  /* not available on *Bridge-EP */
  if ((cpu_model==CPU_SANDYBRIDGE) || (cpu_model==CPU_IVYBRIDGE) ||
(cpu_model==CPU_HASWELL)) {
     result=read_msr(fd,MSR_PP1_ENERGY_STATUS);
     pp1_before=(double)result*energy_units;
     printf("PowerPlane1 (on-core GPU if avail) before: %.6fJ\n",pp1_before);
     result=read_msr(fd,MSR_PP1_POLICY);
     int pp1_policy=(int)result&0x001f;
     printf("PowerPlane1 (on-core GPU if avail) %d policy: %d\n",core,pp1_policy);
  }

/* Despite documentation saying otherwise, it looks like */
/* You can get DRAM readings on regular Haswell */
  if ((cpu_model==CPU_SANDYBRIDGE_EP) || (cpu_model==CPU_IVYBRIDGE_EP) ||
(cpu_model==CPU_HASWELL)) {
     result=read_msr(fd,MSR_DRAM_ENERGY_STATUS);
     dram_before=(double)result*energy_units;
     printf("DRAM energy before: %.6fJ\n",dram_before);
  }

  printf("\nCall Run of Test Suite\n\n");
  int ret = system(command);

  result=read_msr(fd,MSR_PKG_ENERGY_STATUS);
  package_after=(double)result*energy_units;
  printf("Package energy after: %.6f (%.6fJ consumed)\n",
package_after,package_after-package_before);

  result=read_msr(fd,MSR_PP0_ENERGY_STATUS);
  pp0_after=(double)result*energy_units;
  printf("PowerPlane0 (core) for core %d energy after: %.6f (%.6fJ consumed)\n",
core,pp0_after,pp0_after-pp0_before);

  /* not available on SandyBridge-EP */
  if ((cpu_model==CPU_SANDYBRIDGE) || (cpu_model==CPU_IVYBRIDGE) ||
(cpu_model==CPU_HASWELL)) {
     result=read_msr(fd,MSR_PP1_ENERGY_STATUS);
     pp1_after=(double)result*energy_units;
     printf("PowerPlane1 (on-core GPU if avail) after: %.6f (%.6fJ consumed)\n",
pp1_after,pp1_after-pp1_before);
  }

  if ((cpu_model==CPU_SANDYBRIDGE_EP) || (cpu_model==CPU_IVYBRIDGE_EP) ||
(cpu_model==CPU_HASWELL)) {
     result=read_msr(fd,MSR_DRAM_ENERGY_STATUS);
     dram_after=(double)result*energy_units;
     printf("DRAM energy after: %.6f (%.6fJ consumed)\n",
dram_after,dram_after-dram_before);
  }

  printf("\n");
  printf("Note: the energy measurements can overflow in 60s or so\n");
  printf(" so try to sample the counters more often than that.\n\n");
  close(fd);

  return 0;
}

static int perf_event_open(struct perf_event_attr *hw_event_uptr,
                    pid_t pid, int cpu, int group_fd, unsigned long flags) {

        return syscall(__NR_perf_event_open,hw_event_uptr, pid, cpu,
                        group_fd, flags);
}


static int rapl_perf(int core, char * command) {

FILE *fff;
int type;
int config=0;
double scale=0.0;
char units[BUFSIZ];
int fd;
struct perf_event_attr attr;
long long value;

fff=fopen("/sys/bus/event_source/devices/power/type","r");
if (fff==NULL) {
printf("No perf_event rapl support found (requires Linux 3.14)\n");
printf("Falling back to raw msr support\n");
return -1;
}
fscanf(fff,"%d",&type);
fclose(fff);

fff=fopen("/sys/bus/event_source/devices/power/events/energy-pkg","r");
if (fff!=NULL) {
fscanf(fff,"event=%x",&config);
printf("Found config=%d\n",config);
fclose(fff);
}


fff=fopen("/sys/bus/event_source/devices/power/events/energy-pkg.scale","r");
if (fff!=NULL) {
fscanf(fff,"%lf",&scale);
printf("Found config=%g\n",scale);
fclose(fff);
}


fff=fopen("/sys/bus/event_source/devices/power/events/energy-pkg.unit","r");
if (fff!=NULL) {
fscanf(fff,"%s",units);
printf("Found units=%s\n",units);
fclose(fff);
}

attr.type=type;
attr.config=config;

fd=perf_event_open(&attr,-1,core,-1,0);
if (fd<0) {
printf("error opening: %s\n",strerror(errno));
}

printf("\nRunning Test suite \n\n");
printf("Command is: %s\n", command);
int res = system(command);

read(fd,&value,8);

close(fd);

printf("Package Energy Consumed: %lf %s\n",(double)value*scale,units);



#if 0
  /* Calculate the units used */
  result=read_msr(fd,MSR_RAPL_POWER_UNIT);

  power_units=pow(0.5,(double)(result&0xf));
  energy_units=pow(0.5,(double)((result>>8)&0x1f));
  time_units=pow(0.5,(double)((result>>16)&0xf));

  printf("Power units = %.3fW\n",power_units);
  printf("Energy units = %.8fJ\n",energy_units);
  printf("Time units = %.8fs\n",time_units);
  printf("\n");

  /* Show package power info */
  result=read_msr(fd,MSR_PKG_POWER_INFO);
  thermal_spec_power=power_units*(double)(result&0x7fff);
  printf("Package thermal spec: %.3fW\n",thermal_spec_power);
  minimum_power=power_units*(double)((result>>16)&0x7fff);
  printf("Package minimum power: %.3fW\n",minimum_power);
  maximum_power=power_units*(double)((result>>32)&0x7fff);
  printf("Package maximum power: %.3fW\n",maximum_power);
  time_window=time_units*(double)((result>>48)&0x7fff);
  printf("Package maximum time window: %.6fs\n",time_window);

  /* Show package power limit */
  result=read_msr(fd,MSR_PKG_RAPL_POWER_LIMIT);
  printf("Package power limits are %s\n", (result >> 63) ? "locked" : "unlocked");
  double pkg_power_limit_1 = power_units*(double)((result>>0)&0x7FFF);
  double pkg_time_window_1 = time_units*(double)((result>>17)&0x007F);
  printf("Package power limit #1: %.3fW for %.6fs (%s, %s)\n", pkg_power_limit_1, pkg_time_window_1,
           (result & (1LL<<15)) ? "enabled" : "disabled",
           (result & (1LL<<16)) ? "clamped" : "not_clamped");
  double pkg_power_limit_2 = power_units*(double)((result>>32)&0x7FFF);
  double pkg_time_window_2 = time_units*(double)((result>>49)&0x007F);
  printf("Package power limit #2: %.3fW for %.6fs (%s, %s)\n", pkg_power_limit_2, pkg_time_window_2,
          (result & (1LL<<47)) ? "enabled" : "disabled",
          (result & (1LL<<48)) ? "clamped" : "not_clamped");

  printf("\n");

  /* result=read_msr(fd,MSR_RAPL_POWER_UNIT); */

  result=read_msr(fd,MSR_PKG_ENERGY_STATUS);
  package_before=(double)result*energy_units;
  printf("Package energy before: %.6fJ\n",package_before);

  /* only available on *Bridge-EP */
  if ((cpu_model==CPU_SANDYBRIDGE_EP) || (cpu_model==CPU_IVYBRIDGE_EP))
  {
    result=read_msr(fd,MSR_PKG_PERF_STATUS);
    double acc_pkg_throttled_time=(double)result*time_units;
    printf("Accumulated Package Throttled Time : %.6fs\n",acc_pkg_throttled_time);
  }

  result=read_msr(fd,MSR_PP0_ENERGY_STATUS);
  pp0_before=(double)result*energy_units;
  printf("PowerPlane0 (core) for core %d energy before: %.6fJ\n",core,pp0_before);

  result=read_msr(fd,MSR_PP0_POLICY);
  int pp0_policy=(int)result&0x001f;
  printf("PowerPlane0 (core) for core %d policy: %d\n",core,pp0_policy);

  /* only available on *Bridge-EP */
  if ((cpu_model==CPU_SANDYBRIDGE_EP) || (cpu_model==CPU_IVYBRIDGE_EP))
  {
    result=read_msr(fd,MSR_PP0_PERF_STATUS);
    double acc_pp0_throttled_time=(double)result*time_units;
    printf("PowerPlane0 (core) Accumulated Throttled Time : %.6fs\n",acc_pp0_throttled_time);
  }

  /* not available on *Bridge-EP */
  if ((cpu_model==CPU_SANDYBRIDGE) || (cpu_model==CPU_IVYBRIDGE) ||
(cpu_model==CPU_HASWELL)) {
     result=read_msr(fd,MSR_PP1_ENERGY_STATUS);
     pp1_before=(double)result*energy_units;
     printf("PowerPlane1 (on-core GPU if avail) before: %.6fJ\n",pp1_before);
     result=read_msr(fd,MSR_PP1_POLICY);
     int pp1_policy=(int)result&0x001f;
     printf("PowerPlane1 (on-core GPU if avail) %d policy: %d\n",core,pp1_policy);
  }

/* Despite documentation saying otherwise, it looks like */
/* You can get DRAM readings on regular Haswell */
  if ((cpu_model==CPU_SANDYBRIDGE_EP) || (cpu_model==CPU_IVYBRIDGE_EP) ||
(cpu_model==CPU_HASWELL)) {
     result=read_msr(fd,MSR_DRAM_ENERGY_STATUS);
     dram_before=(double)result*energy_units;
     printf("DRAM energy before: %.6fJ\n",dram_before);
  }

  printf("\nRunning Tests");
  int res = system(command);

  result=read_msr(fd,MSR_PKG_ENERGY_STATUS);
  package_after=(double)result*energy_units;
  printf("Package energy after: %.6f (%.6fJ consumed)\n",
package_after,package_after-package_before);

  result=read_msr(fd,MSR_PP0_ENERGY_STATUS);
  pp0_after=(double)result*energy_units;
  printf("PowerPlane0 (core) for core %d energy after: %.6f (%.6fJ consumed)\n",
core,pp0_after,pp0_after-pp0_before);

  /* not available on SandyBridge-EP */
  if ((cpu_model==CPU_SANDYBRIDGE) || (cpu_model==CPU_IVYBRIDGE) ||
(cpu_model==CPU_HASWELL)) {
     result=read_msr(fd,MSR_PP1_ENERGY_STATUS);
     pp1_after=(double)result*energy_units;
     printf("PowerPlane1 (on-core GPU if avail) after: %.6f (%.6fJ consumed)\n",
pp1_after,pp1_after-pp1_before);
  }

  if ((cpu_model==CPU_SANDYBRIDGE_EP) || (cpu_model==CPU_IVYBRIDGE_EP) ||
(cpu_model==CPU_HASWELL)) {
     result=read_msr(fd,MSR_DRAM_ENERGY_STATUS);
     dram_after=(double)result*energy_units;
     printf("DRAM energy after: %.6f (%.6fJ consumed)\n",
dram_after,dram_after-dram_before);
  }
#endif

return 0;
}

int main(int argc, char **argv) {

int c;
int force_msr=0;
int core=0;
int result=-1;
printf("\n");

opterr=0;
if (argc < 2)
{
printf("No argument Provided see Help");
return 1;
}
while ((c = getopt (argc, argv, "c:hm")) != -1) {
switch (c) {
case 'c':
core = atoi(optarg);
break;
case 'h':
printf("Usage: %s [-c core] [-h] [-m] [-r]\n\n",argv[0]);
exit(0);
case 'm':
force_msr = 1;
break;
case 'r':
printf("Running the following: %s \n\n",argv[1]);
break;
default:
exit(-1);
}
}

if (!force_msr) {
result=rapl_perf(core, argv[1]);
}

if (result<0) {
result=rapl_msr(core, argv[1]);
}

if (result<0) {

printf("Unable to read RAPL counters.\n");
printf("* Verify you have an Intel Sandybridge, Ivybridge or Haswell processor\n");
printf("* You may need to run as root or have /proc/sys/kernel/perf_event_paranoid set properly\n");
printf("* If using raw msr access, make sure msr module is installed\n");
printf("\n");

return -1;

}

return 0;
}

