#include "lib.h"

#define MAX_BLOCKS 8388608 //2^23
#define BMAP_BLOCKS 256 //2^23/2^15 = 2^8
#define MAX_OBJS 1048576 //2^20
#define BMAP_OBJS 32 //2^20/2^15 = 2^5
#define HASH_SIZE 1024 //2^10
#define OBJECT_BLOCKS 32768 //2^15
#define OBJS_PER_BLOCK 32 //2^5
#define OBJ_SIZE 128 //2^5

#define CACHE_BLOCKS 32768 //2^27/2^12 = 2^15
#define CACHETABLE_SIZE 32768 //2^15

#define L1_BLOCKS 14
#define L2_BLOCKS 5

#define malloc_4k(x) do{\
                         (x) = mmap(NULL, BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);\
                         if((x) == MAP_FAILED)\
                              (x)=NULL;\
                     }while(0);
#define free_4k(x) munmap((x), BLOCK_SIZE)

typedef unsigned int u32;
typedef 	 int s32;
typedef unsigned short u16;
typedef int      short s16;
typedef unsigned char u8;
typedef char	      s8;

typedef unsigned long u64;
typedef long s64;

struct cache_id{
    u32 block_id;
    u32 cache_id;
    u32 dirty;
    struct cache_id *left;
    struct cache_id *right;
    struct cache_id *next;
};

struct object{
    u32 id;
    u64 size;
    int cache_index;
    int blocks;
    char key[32];
    u32 l1[L1_BLOCKS];
    u32 l2[L2_BLOCKS];
};

struct obj_id{
    u32 id;
    struct obj_id *next;
};

struct obj_id *hashTable[HASH_SIZE] = {NULL};

u64 block_bmap[MAX_BLOCKS>>6] = {0};
u64 inode_bmap[MAX_OBJS>>6] = {0};

u64 metadata_blocks = 0;
u64 obj_metadata_blocks = 0;

u64 cache_bmap[CACHE_BLOCKS>>6] = {0};
struct cache_id *cacheTable[CACHETABLE_SIZE] = {NULL};
struct cache_id *dl_start = NULL;
struct cache_id *dl_end = NULL;

#ifdef CACHE
// Reference used for LRU : https://medium.com/@krishankantsinghal/my-first-blog-on-medium-583159139237

int free_cache()
{
    //implement a lock here
    for(int i=0;i<(CACHE_BLOCKS>>6);i++){
        for(int j=63;j>=0;j--){
            if((cache_bmap[i]>>j)&0x1 != 1){
                cache_bmap[i] = inode_bmap[i] | (1<<j);
                return 64*i + 64 - j;
            }
        }
    }
    return -1;
}

u32 cache_hash(u32 block_id)
{
    //size = CACHETABLE_SIZE
    return (block_id%CACHETABLE_SIZE);
}

struct cache_id* push_to_ct(u32 index, u32 block_id, u32 cache_id)
{
    //lock here
    struct cache_id* head = cacheTable[index];
    struct cache_id* node = (struct cache_id*)malloc(sizeof(struct cache_id));
    node->block_id = block_id;
    node->cache_id = cache_id;
    node->dirty = 0;
    node->left = NULL;
    node->right = NULL;
    node->next = head;
    cacheTable[index] = node;
    return node;
}

void remove_from_ct(u32 index, u32 block_id)
{
    //lock here
    struct cache_id* head = cacheTable[index];
    struct cache_id* prev = cacheTable[index];
    while(head != NULL && head->block_id != block_id) {
        prev = head;
        head = head->next;
    }
    if(head != NULL) {
        if(prev == head) prev = head->next;
        else prev->next = head->next;
        free(head);
    }
}

struct cache_id* get_from_ct(u32 index, u32 block_id)
{
    struct cache_id* head = cacheTable[index];
    while(head != NULL && head->block_id != block_id) head = head->next;
    return head;
}

void dl_add_top(struct cache_id *dl_node)
{
    //lock
    dl_node->right = dl_start;
    dl_node->left = NULL;
    if(dl_start != NULL) dl_start->left = dl_node;
    dl_start = dl_node;
    if(dl_end == NULL) dl_end = dl_start;
}

void dl_remove_entry(struct cache_id *dl_node)
{
    //lock
    if(dl_node->left != NULL) {
        dl_node->left->right = dl_node->right;
    } else {
        dl_start = dl_node->right;
    }

    if (dl_node->right != NULL) {
        dl_node->right->left = dl_node->left;
    } else {
        dl_end = dl_node->left;
    }
}

struct cache_id* put_cache_id(struct objfs_state *objfs, u32 index, u32 block_id)
{
    //lock
    int free_cid = free_cache();
    struct cache_id* dup_end = dl_end;
    struct cache_id* newnode;
    if(free_cid == -1) {
        u32 cid = dup_end->cache_id;
        if(!dup_end->dirty) {
            char *cache_ptr = objfs->cache + (dup_end->block_id << 12);
            if(write_block(objfs, dup_end->block_id, cache_ptr) < 0) return NULL;
        }
        //should be called in this specific order
        dl_remove_entry(dup_end);
        remove_from_ct(cache_hash(dup_end->block_id), dup_end->block_id); //can be optimized we don't need to free this, just change the block_id
        newnode = push_to_ct(index, block_id, cid);
        dl_add_top(newnode);
    } else {
        newnode = push_to_ct(index, block_id, free_cid);
        dl_add_top(newnode);
    }
}

struct cache_id* get_cache_id(struct objfs_state *objfs, u32 index, u32 block_id)
{
    struct cache_id *node = get_from_ct(index, block_id);
    if(node == NULL){
        return put_cache_id(objfs, index, block_id);
    } else {
        dl_remove_entry(node);//hopefully this will work
        dl_add_top(node);
        return node;
    }
    return NULL;
}

void remove_block_cached(u32 block_id)
{
    //lock here
    //this function removes this block from disk
    int i = block_id>>6;
    int j = 64 - (block_id%64);
    block_bmap[i] = block_bmap[i] & ((1<<j) ^ ((1<<64) - 1));
    return ;
}

void remove_object_cached(struct object* obj)
{
    //lock here
    //free this obj before exiting
    int i = obj->id>>6;
    int j = 64 - (obj->id%64);
    inode_bmap[i] = inode_bmap[i] & ((1<<j) ^ ((1<<64) - 1));
    free(obj);
    return ;
}

int find_read_cached(struct objfs_state *objfs, u32 block_id, char *user_buf, int size)
{
    struct cache_id* obj = get_cache_id(objfs, cache_hash(block_id), block_id);
    char *cache_ptr = objfs->cache + (obj->cache_id << 12);
    memcpy(user_buf, cache_ptr, size);
    return size;
}

int find_write_cached(struct objfs_state *objfs, u32 block_id, const char *user_buf, int size)
{
    struct cache_id* obj = get_cache_id(objfs, cache_hash(block_id), block_id);
    char *cache_ptr = objfs->cache + (obj->cache_id << 12);
    memcpy(cache_ptr, user_buf, size);
    obj->dirty = 1;
    return size;
}

int block_sync(struct objfs_state *objfs, struct cache_id* obj)
{
    char *cache_ptr = objfs->cache + (obj->cache_id << 12);
    if(!obj->dirty) return 0;
    if(write_block(objfs, obj->block_id, cache_ptr) < 0) return -1;
    obj->dirty = 0;
    return 0;
}

#else  //uncached implementation

void remove_block_cached(u32 block_id)
{
    //lock here
    //this function removes this block from disk
    int i = block_id>>6;
    int j = 64 - (block_id%64);
    block_bmap[i] = block_bmap[i] & ((1<<j) ^ ((1<<64) - 1));
    return ;
}

void remove_object_cached(struct object* obj)
{
    //lock here
    //free this obj before exiting
    int i = obj->id>>6;
    int j = 64 - (obj->id%64);
    inode_bmap[i] = inode_bmap[i] & ((1<<j) ^ ((1<<64) - 1));
    free(obj);
    return ;
}

int find_read_cached(struct objfs_state *objfs, u32 block_id, char *user_buf, int size)
{
   void *ptr;
   malloc_4k(ptr);
   if(!ptr)
        return -1;
   if(read_block(objfs, block_id, ptr) < 0)
       return -1;
   memcpy(user_buf, ptr, size);
   free_4k(ptr);
   return 0;
}

int find_write_cached(struct objfs_state *objfs, u32 block_id, const char *user_buf, int size)
{
   void *ptr;
   malloc_4k(ptr);
   if(!ptr)
        return -1;
   memcpy(ptr, user_buf, size);
   if(write_block(objfs, block_id, ptr) < 0)
       return -1;
   free_4k(ptr);
   return 0;
}

int block_sync(struct objfs_state *objfs, struct cache_id* node)
{
   return 0;
}

#endif

struct object* load_object(struct objfs_state *objfs, u32 objid)
{
    u32 ob = objid/OBJS_PER_BLOCK;

    void *ptr;
    malloc_4k(ptr);

    char *temp = (char*)malloc(sizeof(struct object));
    char _compare[sizeof(struct object)] = {0};
    if(find_read_cached(objfs, obj_metadata_blocks + ob, ptr, BLOCK_SIZE) < 0) return NULL;

    memcpy(temp, ((struct object*)ptr) + (objid%OBJS_PER_BLOCK), sizeof(struct object));
    if(!memcmp(temp, _compare, sizeof(struct object))){
        //will this be enough to know that we don't have a that inode or do we have to check the bitmap
        free(temp);
        return NULL;
    }
    free_4k(ptr);
    return (struct object*)temp;
}

int store_object(struct objfs_state *objfs, struct object* obj)
{
    u32 ob = obj->id/OBJS_PER_BLOCK;
    u32 objid = obj->id;
    void *ptr;
    malloc_4k(ptr);
    if(find_read_cached(objfs, obj_metadata_blocks + ob, ptr, BLOCK_SIZE) < 0) return -1;
    memcpy(((struct object*)ptr) + (objid%OBJS_PER_BLOCK), obj, sizeof(struct object));

    if(find_write_cached(objfs, obj_metadata_blocks + ob, ptr, BLOCK_SIZE) < 0) return -1;
    free_4k(ptr);

    return (int)obj->id;
}

u32 hash(char *key)
{   //should never return 0 because when we store inode->hash_idx mapping
    //hasidx = 0 means that inode is not allocated, we keep this as an identifier
    //has size = HASH_SIZE
    u32 k = 0;
    for(int i=0;i<32 && key[i]!=0;i++) {
        k = (k * 33) + key[i];
        k %= (MAX_OBJS-1);
    }
    return k+1;
}

int free_inode()
{
    //dprintf("Hritvik: free_inode\n");
    //implement a lock here
    for(int i=0;i<(MAX_OBJS>>6);i++){
        for(int j=63;j>=0;j--){
            if((inode_bmap[i]>>j)&0x1 != 1){
                inode_bmap[i] = inode_bmap[i] | (1<<j);
                //dprintf("Hritvik: free_inode(i,j,inode_bmap[i]) = %d, %d, %d\n", i, j, inode_bmap[i]);
                return 64*i + 64 - j + 2;
            }
        }
    }
    return -1;
}

int free_block()
{
    //dprintf("Hritvik: free_block\n");
    //implement a lock here
    for(int i=0;i<(MAX_BLOCKS>>6);i++){
        for(int j=63;j>=0;j--){
            if((block_bmap[i]>>j)&0x1 != 1){
                block_bmap[i] = block_bmap[i] | (1<<j);
                //dprintf("Hritvik: free_block(i,j,inode_bmap[i], metadata) = %d, %d, %d, %d\n", i, j, inode_bmap[i], metadata_blocks);
                return 64*i + 64 - j + metadata_blocks;
            }
        }
    }
    return -1;
}

void push_to_ht(u32 index, u32 value)
{
    //dprintf("Hritvik: push_to_ht\n");
    //lock here
    struct obj_id* head = hashTable[index];
    struct obj_id* node = (struct obj_id*)malloc(sizeof(struct obj_id));
    node->id = value;
    node->next = head;
    hashTable[index] = node;
}

/*
Returns the object ID.  -1 (invalid), 0, 1 - reserved
*/
long find_object_id(const char *key, struct objfs_state *objfs)
{
    dprintf("Hritvik: find_object_id\n");
    u32 hs = hash(key);
    struct obj_id* fellow = hashTable[hs];
    struct object* freeo = NULL;
    while(fellow!=NULL){
        freeo = load_object(objfs, fellow->id);
        if(!strcmp(freeo->key, key)) break;
        free(freeo);
        fellow = fellow->next;
    }
    if(fellow==NULL) return -1;
    u32 retval = freeo->id;
    free(freeo);
    return retval;
}

/*
  Creates a new object with obj.key=key. Object ID must be >=2.
  Must check for duplicates.

  Return value: Success --> object ID of the newly created object
                Failure --> -1
*/
long create_object(const char *key, struct objfs_state *objfs)
{
    dprintf("Hritvik: create_object\n");
    u32 hs = hash(key);
    struct obj_id* fellow = hashTable[hs];
    struct object* freeo = NULL;
    while(fellow!=NULL){
        freeo = load_object(objfs, fellow->id);
        if(!strcmp(freeo->key, key)) return -1;
        free(freeo);
        fellow = fellow->next;
    }
    int free_id = free_inode();
    if(free_id == -1){
         dprintf("%s: objstore full\n", __func__);
         return -1;
    }
    push_to_ht(hs, free_id);
    freeo = (struct object*)calloc(1, sizeof(struct object));
    strcpy(freeo->key, key);
    freeo->id = free_id;
    store_object(objfs, freeo);
    free(freeo);
    return free_id;
}
/*
  One of the users of the object has dropped a reference
  Can be useful to implement caching.
  Return value: Success --> 0
                Failure --> -1
*/
long release_object(int objid, struct objfs_state *objfs)
{
    return 0;
}

/*
  Destroys an object with obj.key=key. Object ID is ensured to be >=2.

  Return value: Success --> 0
                Failure --> -1
*/
long destroy_object(const char *key, struct objfs_state *objfs)
{
    u32 hs = hash(key);
    struct obj_id* fellow = hashTable[hs];
    struct obj_id* prev = hashTable[hs];
    struct object* obj = NULL;
    while(fellow!=NULL){
        obj = load_object(objfs, fellow->id);
        if(!strcmp(obj->key, key)) break;
        free(obj);
        prev = fellow;
        fellow = fellow->next;
    }
    if(fellow == NULL) return -1;
    else {
        //lock here
        if(prev == fellow) prev = fellow->next;
        else prev->next = fellow->next;
        free(fellow);
    }
    u32 blks = obj->blocks;
    u32 idx = 0;
    while(idx<L1_BLOCKS && blks>0) {
        remove_block_cached(obj->l1[idx]);
        idx++;
        blks--;
    }
    idx=0;
    u32 b2r[1024];
    while(idx<L2_BLOCKS && blks>0) {
        if(find_read_cached(objfs, obj->l2[idx], (char*)b2r, BLOCK_SIZE)<0) return -1;
        for(u32 i=0;i<1024 && blks>0;i++){
            blks--;
            remove_block_cached(b2r[i]);
        }
        remove_block_cached(obj->l2[idx]);
        idx++;
    }
    remove_object_cached(obj);
    return -1;
}

/*
  Renames a new object with obj.key=key. Object ID must be >=2.
  Must check for duplicates.
  Return value: Success --> object ID of the newly created object
                Failure --> -1
*/

long rename_object(const char *key, const char *newname, struct objfs_state *objfs)
{
    u32 hs = hash(newname);
    u32 hso = hash(key);
    struct obj_id* fellow = hashTable[hs];
    struct object* freeo = NULL;
    while(fellow!=NULL){
        freeo = load_object(objfs, fellow->id);
        if(!strcmp(freeo->key, newname)) return -1;
        free(freeo);
        fellow = fellow->next;
    }
    fellow = hashTable[hso];
    struct obj_id* prev = hashTable[hso];
    while(fellow!=NULL){
        freeo = load_object(objfs, fellow->id);
        if(!strcmp(freeo->key, key)) break;
        free(freeo);
        prev = fellow;
        fellow = fellow->next;
    }
    if(fellow == NULL) return -1;
    else {
        //lock here
        push_to_ht(hs, fellow->id);
        if(prev == NULL) prev = fellow->next;
        else prev->next = fellow->next;
        free(fellow);
    }
    strcpy(freeo->key, newname);
    u32 retval = freeo->id;
    store_object(objfs, freeo);
    free(freeo);
    return retval;
}

/*
  Writes the content of the buffer into the object with objid = objid.
  Return value: Success --> #of bytes written
                Failure --> -1
*/
long objstore_write(int objid, const char *buf, int size, struct objfs_state *objfs, off_t offset)
{
    dprintf("Doing write size = %d\n", size);
    struct object* obj = load_object(objfs, objid);
    if(obj==NULL) return -1;
    //TODO: should be implemented in beter way
    u32 retval = size;
    if(offset>>12 > obj->blocks) obj->size += size;

    u32 idx = offset>>12;
    int temp;
    while(size>0&&idx<L1_BLOCKS) {
        if(obj->l1[idx]==0){
            temp = free_block();
            if(temp == -1){
                dprintf("%s: objstore full\n", __func__);
                return -1;
            }
            obj->l1[idx] = temp;
            obj->blocks++;
        }
        if(find_write_cached(objfs, temp, buf+idx*BLOCK_SIZE, size)<0)
            return -1;
        idx++;
        size -= BLOCK_SIZE;
    }
    idx = offset>>22;
    while(size>0&&idx<L2_BLOCKS) {
        u32 b2r[1024] = {0};
        if(obj->l2[idx]==0){
            temp = free_block();
            if(temp == -1){
                dprintf("%s: objstore full\n", __func__);
                return -1;
            }
            obj->l2[idx] = temp;
        } else {
            if(find_read_cached(objfs, obj->l2[idx], (char*)b2r, BLOCK_SIZE)<0) return -1;
        }
        if(size>(BLOCK_SIZE<<10)) {
            for(u32 i=0;i<1024;i++) {
                if(b2r[i] == 0){
                    temp = free_block();
                    if(temp == -1){
                        dprintf("%s: objstore full\n", __func__);
                        return -1;
                    }
                    b2r[i] = temp;
                    obj->blocks++;
                }
            }
        } else {
            u32 temp2 = size/BLOCK_SIZE + (size%BLOCK_SIZE == 0);
            for(u32 i=0;i<temp2;i++) {
                if(b2r[i] == 0){
                    temp = free_block();
                    if(temp == -1){
                        dprintf("%s: objstore full\n", __func__);
                        return -1;
                    }
                    b2r[i] = temp;
                    obj->blocks++;
                }
            }
        }
        //TODO: can put a flag so that i don't have to write again
        if(find_write_cached(objfs, obj->l2[idx], (char*)b2r, BLOCK_SIZE)<0) return -1;
        for(u32 i=0;i<1024 && b2r[i]!=0;i++) {
            if(find_write_cached(objfs, b2r[i], buf+L1_BLOCKS*BLOCK_SIZE+idx*(BLOCK_SIZE<<10) + i*BLOCK_SIZE, size)<0)
                return -1;
            size -= BLOCK_SIZE;
        }
        idx++;
    }
    store_object(objfs, obj);
    free(obj);
    return retval;
}

/*
  Reads the content of the object onto the buffer with objid = objid.
  Return value: Success --> #of bytes read
                Failure --> -1
*/
long objstore_read(int objid, char *buf, int size, struct objfs_state *objfs, off_t offset)
{
    dprintf("Doing read size = %d\n", size);
    struct object* obj = load_object(objfs, objid);

    //TODO: should be implemented in beter way
    u32 retval = size;

    //TODO: change this value to probably one block
    if(offset>>12 > obj->blocks) return -1;

    u32 idx = offset>>12;
    int temp;
    while(size>0&&idx<L1_BLOCKS) {
        if(find_read_cached(objfs, temp, buf+idx*BLOCK_SIZE, size)<0)
            return -1;
        idx++;
        size -= BLOCK_SIZE;
    }
    idx = offset>>22;
    while(size>0&&idx<L2_BLOCKS) {
        u32 b2r[1024] = {0};
        if(find_read_cached(objfs, obj->l2[idx], (char*)b2r, BLOCK_SIZE)<0) return -1;
        for(u32 i=0;i<1024 && b2r[i]!=0 && size>0;i++) {
            if(find_write_cached(objfs, b2r[i], buf+L1_BLOCKS*BLOCK_SIZE+idx*(BLOCK_SIZE<<10) + i*BLOCK_SIZE, size)<0)
                return -1;
            size -= BLOCK_SIZE;
        }
        idx++;
    }
    free(obj);
    return retval;
}

/*
  Reads the object metadata for obj->id = buf->st_ino
  Fillup buf->st_size and buf->st_blocks correctly
  See man 2 stat
*/
int fillup_size_details(struct stat *buf, struct objfs_state* objfs)
{
    if(buf->st_ino<2) return -1;
    struct object *obj = load_object(objfs, buf->st_ino);
    buf->st_size = obj->size;
    buf->st_blocks = obj->blocks;
    free(obj);
    return 0;
}

/*
   Set your private pointer, anyway you like.
*/
int objstore_init(struct objfs_state *objfs)
{
    dprintf("Hritvik: objstore_init");
    //lock
    for(u32 i=0;i<BMAP_BLOCKS;i++){
        if(read_block(objfs, metadata_blocks++, (char*)(block_bmap + (i<<9))) < 0) return -1;
    }
    for(u32 i=0;i<BMAP_OBJS;i++){
        if(read_block(objfs, metadata_blocks++, (char*)(inode_bmap + (i<<9))) < 0) return -1;
    }

    void *ptr;
    malloc_4k(ptr);
    if(!ptr) return -1;
    //restoring inode to hashTable index mapping
    //2^12/2^2 = 2^10 indices per block
    for(u32 i=0;i<(MAX_OBJS>>10);i++){
        if(read_block(objfs, metadata_blocks++, (char*)ptr) < 0) return -1;
        for(u32 j=0;j<1024;j++){
            if(*((u32*)ptr + j) == 0) continue;
            push_to_ht(*((u32*)ptr + j), (i>>10) + j);
        }
    }

    free_4k(ptr);
    obj_metadata_blocks = metadata_blocks;
    //Adding object blocks
    metadata_blocks += OBJECT_BLOCKS;

    //can use objfs->objstore_data;

    dprintf("Done objstore init\n");
    return 0;
}

/*
   Cleanup private data. FS is being unmounted
*/
int objstore_destroy(struct objfs_state *objfs)
{
    metadata_blocks -= OBJECT_BLOCKS;

    u32 hashidx[MAX_OBJS] = {0};
    for(u32 i=1;i<HASH_SIZE;i++) {
        while(hashTable[i]!=NULL) {
            hashidx[hashTable[i]->id] = i;
            hashTable[i] = hashTable[i]->next;
        }
    }

    for(u32 i=(MAX_OBJS>>10);i>=0;i--) {
        if(write_block(objfs, --metadata_blocks, (char*)(hashidx+i)) < 0) return -1;
    }

    for(u32 i=BMAP_OBJS-1;i>=0;i--) {
        if(write_block(objfs, --metadata_blocks, (char*)(inode_bmap + (i<<9))) < 0) return -1;
    }
    for(u32 i=BMAP_BLOCKS-1;i>=0;i--) {
        if(write_block(objfs, --metadata_blocks, (char*)(block_bmap + (i<<9))) < 0) return -1;
    }

    //write the dirty blocks in memory
    for(u32 i=0;i<CACHETABLE_SIZE;i++) {
        if(cacheTable[i]!=NULL) {
            while(cacheTable[i]!=NULL) {
                block_sync(objfs, cacheTable[i]);
                //Note that we are changing the cacheTable pointer here because we won't be using it again
                cacheTable[i] = cacheTable[i]->next;
            }
        }
    }
    //Note: also free the global values
    dprintf("Done objstore destroy\n");
    return 0;
}
