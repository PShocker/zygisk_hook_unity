//
// Created by kotori0 on 2020/2/5.
//

#include <cstring>
#include <unistd.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <dobby.h>
#include <string>
#include "hook_main.h"

#include IL2CPPCLASS

#define DO_API(r, n, p) r (*n) p

#include IL2CPPAPI

#undef DO_API

void init_il2cpp_api() {
#define DO_API(r, n, p) n = (r (*) p)dlsym(il2cpp_handle, #n)

#include IL2CPPAPI

#undef DO_API
}

int isGame(JNIEnv *env, jstring appDataDir) {
    if (!appDataDir)
        return 0;

    const char *app_data_dir = env->GetStringUTFChars(appDataDir, nullptr);

    int user = 0;
    static char package_name[256];
    if (sscanf(app_data_dir, "/data/%*[^/]/%d/%s", &user, package_name) != 2) {
        if (sscanf(app_data_dir, "/data/%*[^/]/%s", package_name) != 1) {
            package_name[0] = '\0';
            LOGW("can't parse %s", app_data_dir);
            return 0;
        }
    }
    env->ReleaseStringUTFChars(appDataDir, app_data_dir);
    if (strcmp(package_name, game_name) == 0) {
        LOGD("detect game: %s", package_name);
        return 1;
    }
    else {
        return 0;
    }
}

unsigned long get_module_base(const char* module_name)
{
    FILE *fp;
    unsigned long addr = 0;
    char *pch;
    char filename[32];
    char line[1024];

    snprintf(filename, sizeof(filename), "/proc/self/maps");

    fp = fopen(filename, "r");

    if (fp != nullptr) {
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, module_name) && strstr(line, "r-xp")) {
                pch = strtok(line, "-");
                addr = strtoul(pch, nullptr, 16);
                if (addr == 0x8000)
                    addr = 0;
                break;
            }
        }
        fclose(fp);
    }
    return addr;
}

typedef void* (*dlopen_type)(const char* name,
                             int flags,
                             //const void* extinfo,
                             const void* caller_addr);
dlopen_type dlopen_backup = nullptr;
void* dlopen_(const char* name,
              int flags,
              //const void* extinfo,
              const void* caller_addr){

    void* handle = dlopen_backup(name, flags, /*extinfo,*/ caller_addr);
    if(!il2cpp_handle){
        LOGI("dlopen: %s", name);
        if(strstr(name, "libil2cpp.so")){
            il2cpp_handle = handle;
            LOGI("Got il2cpp handle at %lx", (long)il2cpp_handle);
        }
    }
    return handle;
}

typedef size_t (*Hook)(char* instance, Il2CppArray* src);
Hook backup = nullptr;
size_t hook(char* instance, Il2CppArray *src){
    if(backup == nullptr){
        LOGE("backup DOES NOT EXIST");
    }
    size_t r = backup(instance, src);
    return r;
}

void hook_each(unsigned long rel_addr, void* hook, void** backup_){
    LOGI("Installing hook at %lx", rel_addr);
    unsigned long addr = /*base_addr + */rel_addr;

    //设置属性可写
    void* page_start = (void*)(addr - addr % PAGE_SIZE);
    if (-1 == mprotect(page_start, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC)) {
        LOGE("mprotect failed(%d)", errno);
        return ;
    }

    DobbyHook(
            reinterpret_cast<void*>(addr),
            hook,
            backup_);
    mprotect(page_start, PAGE_SIZE, PROT_READ | PROT_EXEC);
}

void *hack_thread(void *arg)
{
    LOGI("hack thread: %d", gettid());
    srand(time(nullptr));
    void* loader_dlopen = DobbySymbolResolver(nullptr, "__dl__Z9do_dlopenPKciPK17android_dlextinfoPKv");
    hook_each((unsigned long)loader_dlopen, (void*)dlopen_, (void**)&dlopen_backup);

    while (true)
    {
        base_addr = get_module_base("libil2cpp.so");
        if (base_addr != 0 && il2cpp_handle != nullptr) {
            break;
        }
    }
    LOGD("detect libil2cpp.so %lx, start sleep", base_addr);
    sleep(2);
    LOGD("hack game begin");
    init_il2cpp_api();
    auto* domain = il2cpp_domain_get();
    il2cpp_thread_attach(domain);
    size_t ass_len = 0;
    const Il2CppAssembly** assembly_list = il2cpp_domain_get_assemblies(domain, &ass_len);
    while(strcmp((*assembly_list)->aname.name, "Assembly-CSharp") != 0){
        LOGD("Assembly name: %s", (*assembly_list)->aname.name);
        assembly_list++;
    }
    const Il2CppImage* image = il2cpp_assembly_get_image(*assembly_list);
    Il2CppClass* clazz = il2cpp_class_from_name(image, "Namespace", "Classname");

    hook_each((unsigned long)il2cpp_class_get_method_from_name(clazz, "Your Method", 1)->methodPointer, (void*)hook, (void**)&backup);

    LOGD("hack game finish");
    return nullptr;
}