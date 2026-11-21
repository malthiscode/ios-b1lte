/* SPDX-License-Identifier: BSD-2-Clause
 * Minimal non-Apple device-tree platform for the Exynos 9820 first boot.
 */
#include <pexpert/arm64/board_config.h>
#if defined(B2L)
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOKitKeys.h>
#include <libkern/c++/OSData.h>
#include <pexpert/pexpert.h>

class B2LPlatformExpert : public IODTPlatformExpert {
	OSDeclareDefaultStructors(B2LPlatformExpert);
public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void processTopLevel(IORegistryEntry *root) APPLE_KEXT_OVERRIDE;
	const char *deleteList(void) APPLE_KEXT_OVERRIDE;
	const char *excludeList(void) APPLE_KEXT_OVERRIDE;
	long getGMTTimeOfDay(void) APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(B2LPlatformExpert, IODTPlatformExpert);

bool
B2LPlatformExpert::start(IOService *provider)
{
	IOLog("B2L: starting device-tree platform expert\n");
	if (!IODTPlatformExpert::start(provider)) {
		return false;
	}
    IORegistryEntry *chosen=IORegistryEntry::fromPath("/chosen",gIODTPlane);
    OSData *wifi=chosen?OSDynamicCast(OSData,chosen->getProperty("mac-address-wifi0")):nullptr;
    IOLog("B2L: WiFi MAC metadata %s (address redacted)\n",wifi&&wifi->getLength()==6?"present, 6 bytes":"absent");
    /* B1L-0025: MobileGestalt derives the UDID from /chosen mac-address-*;
     * without them it never answers and lsd never finishes seeding the
     * application database (ios-a5xelte herolte, tools/mkadt_herolte.py;
     * here "Could not get wifi address" 755 times a boot). Synthetic,
     * locally administered addresses, never the phone's. Kill switch:
     * boot-arg b2l_synth_mac=0. */
    {
        int synth=1;PE_parse_boot_argn("b2l_synth_mac",&synth,sizeof(synth));
        static const char *const names[3]={"mac-address-wifi0","mac-address-bluetooth0","mac-address-ethernet0"};
        unsigned added=0;
        for(unsigned i=0;synth&&chosen&&i<3;i++){
            OSData *have=OSDynamicCast(OSData,chosen->getProperty(names[i]));
            if(have&&have->getLength()==6)continue;
            const uint8_t mac[6]={0x02,0x42,0x32,0x4c,0x10,(uint8_t)(i+1)};
            OSData *d=OSData::withBytes(mac,sizeof(mac));
            if(d){chosen->setProperty(names[i],d);d->release();added++;}
        }
        IOLog("B2L: synthetic MAC properties added to /chosen: %u\n",added);
    }
    if(chosen)chosen->release();
    /* B1L-0161: arm XNU derives IOPlatformUUID from /chosen unique-chip-id +
     * chip-id (absent here), so gethostuuid() waited forever and hung
     * configd's IPConfiguration start(). Fixed synthetic UUID, not the
     * phone's; no unique-chip-id so MobileGestalt identity is unchanged. */
    if(!provider->getProperty(kIOPlatformUUIDKey)){
        OSString *u=OSString::withCString("02423240-4c10-5000-8000-0000000000b2");
        if(u){provider->setProperty(kIOPlatformUUIDKey,u);publishResource(kIOPlatformUUIDKey,u);u->release();}
        IOLog("B2L: synthetic IOPlatformUUID published: %s\n",u?"yes":"no");
    }
    /* B1L-0274: hw.model/hw.target (getModelName = first "compatible" of the root) is what MobileGestalt's
     * HWModelStr and MGGetProductType come from; "samsung,beyond2lte" gave product type 0xffffffff, so
     * audiomxd ("ProductID is not defined in GetProductType()": no routing database, no sound) and
     * bluetoothd knew no platform. As a real D94 (and ios-pipa's D22AP root): "D94AP", "iPhone17,2"
     * first, the Samsung string kept after them for the B2L drivers. MobileGestalt's cache is renewed
     * once (b2l_profile_cache.h v6). Kill switch: boot-arg b2l_hw_model=0. */
    {
        int on=1;PE_parse_boot_argn("b2l_hw_model",&on,sizeof(on));
        OSData *c=OSDynamicCast(OSData,provider->getProperty("compatible"));
        bool done=false;
        if(on&&c&&c->getLength()&&strncmp((const char *)c->getBytesNoCopy(),"D94AP",6)){
            static const char head[]="D94AP\0iPhone17,2";   /* sizeof includes the final NUL */
            OSData *n=OSData::withCapacity((unsigned)(sizeof(head)+c->getLength()));
            if(n&&n->appendBytes(head,sizeof(head))&&n->appendBytes(c)){provider->setProperty("compatible",n);done=true;}
            if(n)n->release();
        }
        if(on&&!provider->getProperty("target-sub-type")){
            OSData *t=OSData::withBytes("D94AP",6);if(t){provider->setProperty("target-sub-type",t);t->release();}
        }
        char m[32]="";getModelName(m,sizeof(m));
        IOLog("B2L: root compatible %s; hw.model %s\n",on?(done?"now D94AP, iPhone17,2, samsung,beyond2lte":"left"):"left (b2l_hw_model=0)",m);
    }
    /* B1L-0273 (audio): iOS 26's VirtualAudio knows the phone by IODeviceTree:/product/audio acoustic-id
     * (iPhone17,2 = 8017, DeviceTree.d94ap of 23G90); without it: "Product with Acoustic ID '0' is not handled",
     * no routing database ("Unable to locate category"), and iOS never starts the speaker's I/O (0271/0272).
     * Only that property; /product is made only if absent. Kill switch: boot-arg b2l_audio_adt=0. */
    {
        int on=1;PE_parse_boot_argn("b2l_audio_adt",&on,sizeof(on));
        static const char *const paths[2]={"/product","/product/audio"};
        IORegistryEntry *parent=on?IORegistryEntry::fromPath("/",gIODTPlane):nullptr;
        unsigned made=0;
        for(unsigned i=0;parent&&i<2;i++){
            IORegistryEntry *e=IORegistryEntry::fromPath(paths[i],gIODTPlane);
            if(!e){
                const char *name=paths[i]+(i?9:1);
                e=new IORegistryEntry;
                if(e&&!e->init()){e->release();e=nullptr;}
                if(e){
                    e->setName(name);
                    OSData *n=OSData::withBytes(name,(unsigned)strlen(name)+1);
                    if(n){e->setProperty("name",n);n->release();}
                    if(e->attachToParent(parent,gIODTPlane))made++;else{e->release();e=nullptr;}
                }
            }
            /* B1L-0275: MGGetProductType() matches /product product-id (20 bytes) against its model table;
             * without it VirtualAudio gets ProductID 0 even with hw.model D94AP (0274). Value = D94's public
             * per-model id (DeviceTree.d94ap, 23G90) -> MG type 0x13b8c89e -> VirtualAudio ProductID 0x2b.
             * B1L-0276: off (0275: backboardd aborts, "CoreAnimation: Failed to create SILManager" once MG knows a D94).
             * B1L-0277: b2l_product_id = 0 off, 1 every process (0275), 2 (default) only audiomxd reads it
             * (IOUserClient.cpp b2l_hide_product_id). */
            if(i==0&&e){
                int pid_on=2;PE_parse_boot_argn("b2l_product_id",&pid_on,sizeof(pid_on));
                if(pid_on&&!e->getProperty("product-id")){
                    static const uint8_t d94[20]={0x4f,0xcb,0x57,0xa0,0xbb,0x4e,0x68,0xfc,0x2a,0xe7,
                                                  0x03,0x83,0x73,0x52,0x95,0xe2,0xa9,0xbc,0x69,0x19};
                    OSData *v=OSData::withBytes(d94,sizeof(d94));
                    if(v){e->setProperty("product-id",v);v->release();}
                }
                IOLog("B2L AUDIO: /product product-id %s\n",!pid_on?"off (b2l_product_id=0)":!e->getProperty("product-id")?"FAILED":pid_on==1?"D94 set, every process":"D94 set, audiomxd only");
            }
            parent->release();parent=e;
        }
        if(parent&&!parent->getProperty("acoustic-id")){
            const uint32_t id=8017;OSData *v=OSData::withBytes(&id,sizeof(id));
            if(v){parent->setProperty("acoustic-id",v);v->release();}
        }
        IOLog("B2L AUDIO: device tree /product/audio acoustic-id=8017 %s (nodes made %u)\n",on?(parent?"set":"FAILED"):"off (b2l_audio_adt=0)",made);
        if(parent)parent->release();
    }
    uint32_t sleepDisabled=0;
    if(PE_get_default("sleep-disabled",&sleepDisabled,sizeof(sleepDisabled)) && sleepDisabled)
        IOLog("B2L PM: system sleep disabled until CPU suspend/resume support is available\n");
	/* B1L-0207: we are the time source (getGMTTimeOfDay); without IORTC the calendar waited 30 s, then 1970. */
	publishResource("IORTC");
	registerService();
	IOLog("B2L: platform expert registered; software product iPhone17,2, Samsung B2L hardware\n");
	return true;
}

/* B1L-0207: boot calendar = the PMIC RTC, never earlier than this kernel's build time (written by
 * b1l-build.sh), so TLS certificates are in their validity window from the first second.
 * shortcut: iOS's own time changes are not written back to the RTC; add setGMTTimeOfDay if the RTC drifts. */
#if __has_include("b2l_build_epoch.h")
#include "b2l_build_epoch.h"
#else
#define B2L_BUILD_EPOCH 0
#endif
extern "C" int64_t b2l_pmic_rtc_seconds(void);
long B2LPlatformExpert::getGMTTimeOfDay(void)
{
	/* the PMIC is read once; later callers (IOPMrootDomain) get that time plus the uptime since */
	static int64_t base;
	static uint64_t at;
	if (!base) {
		int64_t rtc = b2l_pmic_rtc_seconds();
		at = mach_absolute_time();
		base = rtc > B2L_BUILD_EPOCH ? rtc : B2L_BUILD_EPOCH;
		IOLog("B2L RTC: boot calendar %lld (%s)\n", base, base == rtc ? "PMIC RTC" : "build time");
	}
	uint64_t ns;
	absolutetime_to_nanoseconds(mach_absolute_time() - at, &ns);
	return (long)(base + (int64_t)(ns / NSEC_PER_SEC));
}

const char *B2LPlatformExpert::deleteList(void) { return "()"; }
const char *B2LPlatformExpert::excludeList(void)
{
	return "('chosen', 'defaults', 'cpus', 'options')";
}

void
B2LPlatformExpert::processTopLevel(IORegistryEntry *root)
{
	/* There is no Apple NVRAM /options service.  Publish the real boot CPU
	 * without fabricating a firmware NVRAM device. */
	IORegistryEntry *cpus = root->childFromPath("cpus", gIODTPlane);
	if (cpus != nullptr) {
		createNubs(this, IODTFindMatchingEntries(cpus, kIODTExclusive, nullptr));
		cpus->release();
	}
	createNubs(this, IODTFindMatchingEntries(root, kIODTExclusive, excludeList()));
    IORegistryEntry *armio=root->childFromPath("arm-io",gIODTPlane);
    if(armio){createNubs(this,IODTFindMatchingEntries(armio,kIODTExclusive,"('gic')"));armio->release();}

}
#endif
