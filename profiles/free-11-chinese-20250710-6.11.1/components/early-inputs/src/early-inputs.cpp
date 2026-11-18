// Synthetic simulator bootstrap. All failures are fatal; existing state is never reset.
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef __linux__
#include <sys/vfs.h>
#endif
#include <sys/statvfs.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <sys/system_properties.h>
#include <sys/xattr.h>
#endif
static bool data_mount_matches(unsigned long root_device, unsigned long data_device,
                               unsigned long user_device, unsigned long database_device, long filesystem_type) {
    return root_device != data_device && data_device == user_device && data_device == database_device && filesystem_type == 0xef53;
}
#ifdef __linux__
static bool ready_data_mount(dev_t* data_device) {
    struct stat root{}, data{}, user{}, database_dir{}; struct statfs filesystem{};
    if (stat("/",&root) || stat("/data",&data) || stat("/data/user_de/0",&user) ||
        lstat("/data/user_de/0/com.qinggan.providers.settings/files/databases",&database_dir) ||
        statfs("/data",&filesystem) || !S_ISDIR(data.st_mode) || !S_ISDIR(user.st_mode) ||
        !S_ISDIR(database_dir.st_mode) ||
        !data_mount_matches(root.st_dev,data.st_dev,user.st_dev,database_dir.st_dev,filesystem.f_type)) return false;
    *data_device=data.st_dev; return true;
}
#endif
#ifdef __ANDROID__
using LabelCheck = bool (*)(int, const char*);
static bool same_inode(const struct stat& a,const struct stat& b) {
    return S_ISDIR(a.st_mode) && S_ISDIR(b.st_mode) && a.st_dev==b.st_dev && a.st_ino==b.st_ino;
}
static int child_directory(int parent,const char* name,dev_t device,uid_t uid,gid_t gid,mode_t mode,LabelCheck label,bool allow_system) {
    bool made=false;
    if(mkdirat(parent,name,mode)) { if(errno!=EEXIST) return -1; } else made=true;
    int path=openat(parent,name,O_PATH|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(path<0) return -1;
    struct stat opened{}, current{};
    bool private_label=label(path,"u:object_r:can_early_data_file:s0");
    bool system_label=allow_system && !made && label(path,"u:object_r:system_app_data_file:s0");
    bool ok=!fstat(path,&opened) && !fstatat(parent,name,&current,AT_SYMLINK_NOFOLLOW) &&
        same_inode(opened,current) && opened.st_dev==device && (private_label || system_label);
    if(ok && made) {
        int rw=openat(path,".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        struct stat check{};
        ok=private_label && rw>=0 && !fstat(rw,&check) && same_inode(opened,check) && !fchmod(rw,mode) && !fchown(rw,uid,gid);
        if(rw>=0) close(rw);
    }
    if(ok) ok=!fstat(path,&opened) && !fstatat(parent,name,&current,AT_SYMLINK_NOFOLLOW) &&
        same_inode(opened,current) && opened.st_dev==device && opened.st_uid==uid && opened.st_gid==gid &&
        (opened.st_mode & 07777)==mode;
    if(!ok) { close(path); return -1; }
    return path;
}
static int prepare_package(int package,dev_t device,uid_t uid,gid_t gid,LabelCheck label,int* files_out) {
    struct stat st{};
    if(fstat(package,&st) || !S_ISDIR(st.st_mode) || st.st_dev!=device || st.st_uid!=uid || st.st_gid!=gid ||
       !label(package,"u:object_r:system_app_data_file:s0")) return -1;
    int files=child_directory(package,"files",device,uid,gid,0771,label,true);
    if(files<0) return -1;
    int db_path=child_directory(files,"databases",device,uid,gid,0770,label,false);
    if(db_path<0) {close(files);return -1;}
    int db=openat(db_path,".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    struct stat a{},b{};
    bool ok=db>=0 && !fstat(db,&a) && !fstat(db_path,&b) && same_inode(a,b);
    close(db_path);
    if(!ok) {if(db>=0)close(db);close(files);return -1;}
    *files_out=files;return db;
}
static bool chain_matches(int user,int package,int files,int db,const struct stat& expected_package) {
    struct stat package_entry{}, files_stat{}, db_stat{}, entry{};
    return !fstat(files,&files_stat) && !fstat(db,&db_stat) &&
        !fstatat(user,"com.qinggan.providers.settings",&package_entry,AT_SYMLINK_NOFOLLOW) && same_inode(expected_package,package_entry) &&
        !fstatat(package,"files",&entry,AT_SYMLINK_NOFOLLOW) && same_inode(files_stat,entry) &&
        !fstatat(files,"databases",&entry,AT_SYMLINK_NOFOLLOW) && same_inode(db_stat,entry);
}

static bool seed_at(int directory,const char* source,uid_t uid,gid_t gid) {
    constexpr const char* dst="qinggan_settings.db";
    constexpr const char* temp="qinggan_settings.db.seed-new";
    struct stat existing{};
    if(!fstatat(directory,dst,&existing,AT_SYMLINK_NOFOLLOW))
        return S_ISREG(existing.st_mode) && existing.st_uid==uid;
    if(errno!=ENOENT) return false;
    int src=open(source,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(src<0) return false;
    struct stat input{};
    if(fstat(src,&input) || !S_ISREG(input.st_mode) || input.st_size<=0 || input.st_size>16*1024*1024) {close(src);return false;}
    if(!fstatat(directory,temp,&existing,AT_SYMLINK_NOFOLLOW)) {
        if(!S_ISREG(existing.st_mode) || (existing.st_uid!=0 && existing.st_uid!=uid) ||
            (existing.st_mode&0077) || unlinkat(directory,temp,0)) {close(src);return false;}
    } else if(errno!=ENOENT) {close(src);return false;}
    int out=openat(directory,temp,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
    if(out<0) {close(src);return false;}
    bool ok=true; char buf[8192]; ssize_t n;
    while((n=read(src,buf,sizeof(buf)))>0) {
        for(ssize_t i=0;i<n;) {ssize_t w=write(out,buf+i,static_cast<size_t>(n-i));if(w<=0){ok=false;break;}i+=w;}
        if(!ok) break;
    }
    if(n<0 || fchmod(out,0600) || fchown(out,uid,gid) || fsync(out)) ok=false;
    if(close(out)) ok=false;
    close(src);
    if(ok && linkat(directory,temp,directory,dst,0)) {
        if(errno!=EEXIST || fstatat(directory,dst,&existing,AT_SYMLINK_NOFOLLOW) || !S_ISREG(existing.st_mode) || existing.st_uid!=uid) ok=false;
    }
    if(unlinkat(directory,temp,0)) ok=false;
    if(fsync(directory)) ok=false;
    return ok;
}

static bool label_for_fd(int fd,const char* expected) {
    char path[64], value[128]{};
    if(snprintf(path,sizeof(path),"/proc/self/fd/%d",fd)>=static_cast<int>(sizeof(path))) return false;
    ssize_t count=getxattr(path,"security.selinux",value,sizeof(value));
    size_t n=strlen(expected);
    return (count==static_cast<ssize_t>(n) || (count==static_cast<ssize_t>(n+1) && value[n]==0)) && !memcmp(value,expected,n);
}
static int path_child(int parent,const char* name) {
    return openat(parent,name,O_PATH|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
}
static bool prepare_de_seed() {
    int root=open("/",O_PATH|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(root<0) return false;
    int data=path_child(root,"data");
    int users=data<0?-1:path_child(data,"user_de");
    int user=users<0?-1:path_child(users,"0");
    int package=user<0?-1:path_child(user,"com.qinggan.providers.settings");
    struct stat r{},d{},u{},p{},now{}; struct statfs fs{};
    bool ok=package>=0 && !fstat(root,&r) && !fstat(data,&d) && !fstat(user,&u) && !fstat(package,&p) &&
        !statfs("/data",&fs) && r.st_dev!=d.st_dev && d.st_dev==u.st_dev && u.st_dev==p.st_dev && fs.f_type==0xef53 &&
        !fstatat(user,"com.qinggan.providers.settings",&now,AT_SYMLINK_NOFOLLOW) && same_inode(p,now);
int files=-1;
int db=ok?prepare_package(package,d.st_dev,1000,1000,label_for_fd,&files):-1;
ok=db>=0 && files>=0 && chain_matches(user,package,files,db,p) &&
    seed_at(db,"/system/etc/voyemu/qg-settings-guest.db",1000,1000) && chain_matches(user,package,files,db,p);
if(db>=0) close(db);
if(files>=0) close(files);
    if(package>=0) close(package);
    if(user>=0) close(user);
    if(users>=0) close(users);
    if(data>=0) close(data);
    close(root); return ok;
}
#endif
#ifndef EARLY_INPUTS_TEST
#ifdef __ANDROID__
static bool expected_label(const char* path, const char* expected) {
    char value[128]{};
    ssize_t count=lgetxattr(path,"security.selinux",value,sizeof(value));
    size_t n=strlen(expected);
    return (count==static_cast<ssize_t>(n) || (count==static_cast<ssize_t>(n+1) && value[n]==0)) && !memcmp(value,expected,n);
}
static bool verify_database() {
    dev_t data_device{}; struct stat database{};
    const char* db="/data/user_de/0/com.qinggan.providers.settings/files/databases/qinggan_settings.db";
    return geteuid()==1000 && ready_data_mount(&data_device) && !lstat(db,&database) &&
        S_ISREG(database.st_mode) && database.st_uid==1000 && database.st_gid==1000 &&
        database.st_dev==data_device && expected_label(db,"u:object_r:can_early_data_file:s0");
}
static bool verify_fixture() {
    const char* image="/system/etc/voyemu/hybrid-nvram.img";
    // The system-UID worker verifies its 0700 application directory first.
    // Init resets this property every boot; only the completed file check sets 1.
    char stage[PROP_VALUE_MAX]{};
    __system_property_get("sys.voyemu.early.ready",stage);
    if(geteuid()!=0 || (strcmp(stage,"db") && strcmp(stage,"1"))) return false;
    struct statvfs root_mount{};
    if(statvfs("/",&root_mount) || !(root_mount.f_flag & ST_RDONLY)) return false;
    struct stat data{}, account{};
    if(stat("/private/configs/token",&account) || !S_ISDIR(account.st_mode)) return false;
    if(!lstat("/private/configs/token/accountInfo",&account) || errno!=ENOENT) return false;
    char user[PROP_VALUE_MAX]{}, multi[PROP_VALUE_MAX]{};
    if(__system_property_get("persist.sys.qg.userid",user)!=0 ||
       __system_property_get("persist.sys.qg.multiuser",multi)!=1 || strcmp(multi,"0")) return false;
    int backing=open(image,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(backing<0) return false;
    bool ok=!fstat(backing,&data) && S_ISREG(data.st_mode) && data.st_uid==0 &&
        !(data.st_mode&0222) && data.st_size==0x22000;
    close(backing); return ok;
}
#endif
int main(int argc, char** argv) {
    alarm(10);
    if(argc!=2) return 1;
#ifdef __ANDROID__
    if(!strcmp(argv[1],"prepare-db")) {
        if(geteuid()!=1000 || !prepare_de_seed() || !verify_database()) return 11;
        return __system_property_set("sys.voyemu.early.ready", "db") ? 12 : 0;
    }
    if(!strcmp(argv[1],"verify") && verify_fixture())
        return __system_property_set("sys.voyemu.early.ready", "1") ? 1 : 0;
#endif
    return 1;
}
#endif
