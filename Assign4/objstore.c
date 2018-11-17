#include "lib.h"

typedef unsigned int u32;
typedef 	 int s32;
typedef unsigned short u16;
typedef int      short s16;
typedef unsigned char u8;
typedef char	      s8;

typedef unsigned long u64;
typedef long s64;

#define MAX_BLOCKS (u64)8388608 //2^35/2^12 = 2^23
#define BMAP_BLOCKS (u64)256 //2^23/2^15 = 2^8
#define MAX_OBJS (u64)1048576 //2^20
#define BMAP_OBJS (u64)32 //2^20/2^15 = 2^5
#define HASH_SIZE (u64)32768 //2^15
#define OBJECT_BLOCKS (u64)32768 //2^15
#define OBJS_PER_BLOCK (u64)32 //2^5
#define OBJ_SIZE (u64)128 //2^5

#define CACHE_BLOCKS (u64)32768 //2^27/2^12 = 2^15
#define CACHETABLE_SIZE (u64)32768 //2^15

#define L1_BLOCKS (u64)14
#define L2_BLOCKS (u64)4

#define malloc_4k(x) do{\
                         (x) = mmap(NULL, BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);\
                         if((x) == MAP_FAILED)\
                              (x)=NULL;\
                     }while(0);
#define free_4k(x) munmap((x), BLOCK_SIZE)

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
            if((u64)((cache_bmap[i]>>j)&1) != 1){
                cache_bmap[i] = cache_bmap[i] | ((u64)1<<j);
                return 64*i + 63 - j;
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
    struct cache_id* node = (struct cache_id*)calloc(1,sizeof(struct cache_id));
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
        if(prev == head) cacheTable[index] = head->next;
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
    //dprintf("Hritvik %s free_cid %d\n",__func__,free_cid);
    struct cache_id* dup_end = dl_end;
    struct cache_id* newnode;
    if(free_cid == -1) {
        // dprintf("Hritvik %s yofree_cid == -1\n",__func__);
        u32 cid = dup_end->cache_id;
        block_sync(objfs, dup_end);
        //should be called in this specific order
        dl_remove_entry(dup_end);
        remove_from_ct(cache_hash(dup_end->block_id), dup_end->block_id); //can be optimized we don't need to free this, just change the block_id
        newnode = push_to_ct(index, block_id, cid);
        dl_add_top(newnode);
        char *cache_ptr = objfs->cache + (cid << 12);
        if(read_block(objfs, block_id, cache_ptr) < 0)return NULL;
    } else {
        // dprintf("Hritvik %s free_cid!=-1\n",__func__);
        newnode = push_to_ct(index, block_id, free_cid);
        //dprintf("Hritvik %s\n",__func__);
        dl_add_top(newnode);
        // if(dl_start==newnode)dprintf("Hritvik start = newnode %s\n",__func__);
        // if(dl_end==newnode)dprintf("Hritvik end = newnode %s\n",__func__);
        //dprintf("Hritvik %s objfs->cache = %lx\n",__func__,objfs->cache);
        char *cache_ptr = objfs->cache + (free_cid << 12);
        if(read_block(objfs, block_id, cache_ptr) < 0)return NULL;
        //dprintf("Hritvik %s after read %s\n",__func__,cache_ptr);
    }
    return newnode;
}

struct cache_id* get_cache_id(struct objfs_state *objfs, u32 index, u32 block_id)
{
    // dprintf("Hritvik %s\n",__func__);
    struct cache_id *node = get_from_ct(index, block_id);
    // dprintf("Hritvik %s\n",__func__);
    if(node == NULL){
        // dprintf("Hritvik %s in NULL\n",__func__);
        return put_cache_id(objfs, index, block_id);
    } else {
        // dprintf("Hritvik %s not in null block_id = %d, index= %d cache_id= %d dirty=%d\n",__func__,block_id,index,node->cache_id,node->dirty);
        dl_remove_entry(node);//hopefully this will work
        dl_add_top(node);
        return node;
    }
    return NULL;
}


int find_read_cached(struct objfs_state *objfs, u32 block_id, char *user_buf, int size)
{
    // dprintf("hritvik In read cached implementation\n");
    struct cache_id* obj = get_cache_id(objfs, cache_hash(block_id), block_id);
    //dprintf("Hritvik %s cache id %lx\n",__func__,obj->cache_id);
    char *cache_ptr = objfs->cache + (obj->cache_id << 12);
    memcpy(user_buf, cache_ptr, size);
    return size;
}

int find_write_cached(struct objfs_state *objfs, u32 block_id, const char *user_buf, int size)
{
    // dprintf("hritvik In write cached implementation\n");
    struct cache_id* obj = get_cache_id(objfs, cache_hash(block_id), block_id);
    // dprintf("Hritvik %s\n",__func__);
    char *cache_ptr = objfs->cache + (obj->cache_id << 12);
    memcpy(cache_ptr, user_buf, size);
    obj->dirty = 1;
    return size;
}

int block_sync(struct objfs_state *objfs, struct cache_id* obj)
{
    //TODO: does this go the staring of the dll
    // dprintf("Hritvik %s",__func__);
    char *cache_ptr = objfs->cache + (obj->cache_id << 12);
    if(!obj->dirty) return 0;
    if(write_block(objfs, (obj->block_id), cache_ptr) < 0) return -1;
    obj->dirty = 0;
    return 0;
}

#else  //uncached implementation

int find_read_cached(struct objfs_state *objfs, u32 block_id, char *user_buf, int size)
{
    void *ptr;
    malloc_4k(ptr);
    if(!ptr) return -1;
    if(read_block(objfs, block_id, ptr) < 0){
        free_4k(ptr);
        return -1;
    }
    memcpy(user_buf, ptr, size);
    free_4k(ptr);
    return 0;
}

int find_write_cached(struct objfs_state *objfs, u32 block_id, const char *user_buf, int size)
{
    // dprintf("Hritvik is in find_write_cached with size = %d\n",size);
   void *ptr;
   malloc_4k(ptr);
   if(!ptr) return -1;
//    dprintf("Hritvik is in find_write_cached\n");
   memcpy(ptr, user_buf, size);
//    dprintf("Hritvik is in find_write_cached\n");
   if(write_block(objfs, block_id, ptr) < 0){
        free_4k(ptr);
        return -1;
    }
    // dprintf("Hritvik is in find_write_cached\n");
   free_4k(ptr);
   return 0;
}

int block_sync(struct objfs_state *objfs, struct cache_id* node)
{
   return 0;
}

#endif

void remove_block_cached(u32 block_id)
{
    //lock here
    //this function removes this block from disk
    block_id -= (metadata_blocks + 1);
    int i = block_id>>6;
    int j = 63 - (block_id%64);
    block_bmap[i] = block_bmap[i] & ((u64)(1<<j) ^ ((u64)(-1)));
    return ;
}

void remove_object_cached(struct objfs_state *objfs, struct object* obj)
{
    //lock here
    //free this obj before exiting
    obj->id -= 2;
    int i = obj->id>>6;
    int j = 63 - (obj->id%64);
    // dprintf("hritvik remove_object_cached i,j,inode_bmap %d, %d, %lx, %lx\n",i,j,inode_bmap[i],(((u64)1<<j) ^ (((u64)1<<63) - 1)));
    inode_bmap[i] = inode_bmap[i] & (((u64)1<<j) ^ ((u64)(-1)));
    // dprintf("hritvik remove_object_cached inode_bmap %lx\n",inode_bmap[i]);
    free(obj);
    return ;
}

struct object* load_object(struct objfs_state *objfs, u32 objid)
{
    // dprintf("Hritvik: %s objid %d\n",__func__,objid);
    int i = (objid-2)>>6;
    int j = 63 - ((objid-2)%64);
    if((u64)((inode_bmap[i]>>j)&1) != 1) return NULL;

    u32 ob = objid/OBJS_PER_BLOCK;
    void *ptr;
    malloc_4k(ptr);

    struct object *temp = (struct object*)malloc(sizeof(struct object));
    //char _compare[sizeof(struct object)] = {0};
    if(find_read_cached(objfs, obj_metadata_blocks + ob, ptr, BLOCK_SIZE) < 0){
        free(temp);
        free_4k(ptr);
        return NULL;
    }
    memcpy(temp, ((struct object*)ptr) + (objid%OBJS_PER_BLOCK), sizeof(struct object));
    // if(!memcmp(temp, _compare, sizeof(struct object))){
    //     //will this be enough to know that we don't have a that inode or do we have to check the bitmap
    //     free(temp);
    //     return NULL;
    // }
    free_4k(ptr);
    return temp;
}

int store_object(struct objfs_state *objfs, struct object* obj)
{
    u32 ob = obj->id/OBJS_PER_BLOCK;
    u32 objid = obj->id;
    void *ptr;
    malloc_4k(ptr);
    // dprintf("Hritvik: %s\n",__func__);
    if(find_read_cached(objfs, obj_metadata_blocks + ob, ptr, BLOCK_SIZE) < 0){
        free_4k(ptr);
        return -1;
    }
    // dprintf("Hritvik %s\n",__func__);
    memcpy(((struct object*)ptr) + (objid%OBJS_PER_BLOCK), obj, sizeof(struct object));

    if(find_write_cached(objfs, obj_metadata_blocks + ob, ptr, BLOCK_SIZE) < 0){
        free_4k(ptr);
        return -1;
    }
    // dprintf("Hritvik %s\n",__func__);
    free_4k(ptr);
    return (int)obj->id;
}

u32 hash(const char *key)
{   //should never return 0 because when we store inode->hash_idx mapping
    //hasidx = 0 means that inode is not allocated, we keep this as an identifier
    //has size = HASH_SIZE
    u32 k = 0;
    for(int i=0;i<32 && key[i]!=0;i++) {
        k = (k * 33) + key[i];
        k %= (HASH_SIZE-1);
    }
    return k + 1;
}

int free_inode()
{
    //implement a lock here
    for(int i=0;i<(MAX_OBJS>>6);i++){
        for(int j=63;j>=0;j--){
            if((u64)((inode_bmap[i]>>j)&1) != 1){
                inode_bmap[i] = inode_bmap[i] | ((u64)1<<j);
                return 64*i + 64 - j + 1;
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
            if((u64)((block_bmap[i]>>j)&1) != 1){
                block_bmap[i] = block_bmap[i] | ((u64)1<<j);
                //dprintf("Hritvik: free_block(i,j,inode_bmap[i], metadata) = %d, %d, %d, %d\n", i, j, inode_bmap[i], metadata_blocks);
                return 64*i + 64 - j + metadata_blocks;
            }
        }
    }
    return -1;
}

void push_to_ht(u32 index, u32 value)
{
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
    u32 hs = hash(key);
    // dprintf("Hritvik: %s hash %d\n",__func__,hs);
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
    // dprintf("Hritvik: create_object %s\n",key);
    u32 hs = hash(key);
    struct obj_id* fellow = hashTable[hs];
    struct object* freeo = NULL;
    while(fellow!=NULL){
        // dprintf("I am here 1\n");
        freeo = load_object(objfs, fellow->id);
        if(!strcmp(freeo->key, key)){
            free(freeo);
            return -1;
        }
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
    freeo->size = 0;
    freeo->blocks = 0;
    // dprintf("Hritvik: create_object %s\n",key);
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
    // dprintf("Hritvik: destroy_object\n");
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
        if(prev == fellow) hashTable[hs] = fellow->next;
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
    remove_object_cached(objfs, obj);
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
    if(*key == '/') key = key+1;
    if(*newname == '/')newname = newname+1;
    // dprintf("Hritvik: rename_object old %s new %s\n",key,newname);
    u32 hs = hash(newname);
    u32 hso = hash(key);
    struct obj_id* fellow = hashTable[hs];
    struct object* freeo = NULL;
    // dprintf("Hritvik: rename_object old %d new %d\n",hso,hs);
    while(fellow!=NULL){
        freeo = load_object(objfs, fellow->id);
        if(!strcmp(freeo->key, newname)) return -1;
        free(freeo);
        fellow = fellow->next;
    }
    // dprintf("Hritvik: rename_object old %s new %s\n",key,newname);
    fellow = hashTable[hso];
    // if(fellow==NULL) dprintf("Hritvik: rename_object old %s new %s fellow is null\n",key,newname);
    struct obj_id* prev = hashTable[hso];
    while(fellow!=NULL){
        // dprintf("Hritvik: rename_object fellow->id %d\n",fellow->id);
        freeo = load_object(objfs, fellow->id);
        if(!strcmp(freeo->key, key)) break;
        free(freeo);
        prev = fellow;
        fellow = fellow->next;
    }
    // dprintf("Hritvik: rename_object old %s new %s\n",key,newname);
    // if(fellow==NULL) dprintf("Hritvik: rename_object old %s new %s fellow is null\n",key,newname);
    if(fellow == NULL) return -1;
    else {
        //lock here
        push_to_ht(hs, fellow->id);
        if(prev == fellow) hashTable[hso] = fellow->next;
        else prev->next = fellow->next;
        free(fellow);
    }
    strcpy(freeo->key, newname);
    u32 retval = freeo->id;
    store_object(objfs, freeo);
    free(freeo);
    // dprintf("Hritvik: rename_object end old %s new %s\n",key,newname);
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
    // dprintf("block bmap %lx\n",block_bmap[0]);
    struct object* obj = load_object(objfs, objid);
    if(obj==NULL) return -1;
    //TODO: should be implemented in beter way
    u32 retval = size;
    // dprintf("Hritvik: objstore_write offset %d\n", offset>>12);
    if(offset>>12 >= obj->blocks) obj->size += size;
    // dprintf("Hritvik: objstore_write obj->size %d\n", obj->size);
    u64 idx = offset>>12;
    int temp;
    while(size>0&&idx<L1_BLOCKS) {
        //dprintf("Hritvik: objstore_write loop 1 %d\n",idx);
        if(obj->l1[idx]==0){
            temp = free_block();
            if(temp == -1){
                free(obj);
                dprintf("%s: objstore full\n", __func__);
                return -1;
            }
            obj->l1[idx] = temp;
            obj->blocks++;
        }
        //dprintf("Hritvik: objstore_write loop 1 l1 %d\n",obj->l1[idx]);
        if(find_write_cached(objfs, temp, buf+idx*BLOCK_SIZE, size)<0){
            free(obj);
            return -1;
        }
        idx++;
        size -= BLOCK_SIZE;
    }
    idx = offset>>22;
    while(size>0&&idx<L2_BLOCKS) {
        u32 l2_level_id = ((offset>>12)-L1_BLOCKS) - (idx<<10);
        // dprintf("Hritvik: objstore_write loop 2 %d\n",idx);
        u32 b2r[1024] = {0};
        if(obj->l2[idx]==0){
            temp = free_block();
            if(temp == -1){
                free(obj);
                dprintf("%s: objstore full\n", __func__);
                return -1;
            }
            obj->l2[idx] = temp;
            if(find_write_cached(objfs, obj->l2[idx], (char*)b2r, BLOCK_SIZE)<0){
                free(obj);
                return -1;
            }
        } else {
            if(find_read_cached(objfs, obj->l2[idx], (char*)b2r, BLOCK_SIZE)<0){
                free(obj);
                return -1;
            }
        }
        //dprintf("Hritvik: objstore_write loop 2 l2_level_id %d\n",l2_level_id);
        if(b2r[l2_level_id] == 0) {
            temp = free_block();
            if(temp == -1){
                free(obj);
                dprintf("%s: objstore full\n", __func__);
                return -1;
            }
            b2r[l2_level_id] = temp;
            obj->blocks++;
            if(find_write_cached(objfs, obj->l2[idx], (char*)b2r, BLOCK_SIZE)<0){
                free(obj);
                return -1;
            }
        }
        // dprintf("%c\n",*(buf+L1_BLOCKS*BLOCK_SIZE+idx*(BLOCK_SIZE<<10) + l2_level_id*BLOCK_SIZE + BLOCK_SIZE-1));
        // dprintf("After allocation the l2 block\n");
        if(find_write_cached(objfs, b2r[l2_level_id], buf+L1_BLOCKS*BLOCK_SIZE+idx*(BLOCK_SIZE<<10) + l2_level_id*BLOCK_SIZE, size)<0){
            free(obj);
            return -1;
        }
        // dprintf("After allocation the l2 block\n");
        size -= BLOCK_SIZE;
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
    // dprintf("Hritvik objstore_read obj->size, obj->blocks -> %d %d\n", obj->size, obj->blocks);
    //TODO: should be implemented in beter way
    if(obj==NULL) return -1;
    u32 retval = size;

    //TODO: change this value to probably one block
    if(offset>>12 > obj->blocks) return -1;

    u64 idx = offset>>12;
    while(size>0&&idx<L1_BLOCKS) {
        // dprintf("Hritvik: objstore_read loop 1 %d\n",idx);
        if(find_read_cached(objfs, obj->l1[idx], buf+idx*BLOCK_SIZE, (size>BLOCK_SIZE)?BLOCK_SIZE:size)<0)
            return -1;
        //dprintf("Hritvik: objstore_read loop 1 buf %s\n", buf);
        idx++;
        size -= BLOCK_SIZE;
    }
    idx = offset>>22;
    while(size>0&&idx<L2_BLOCKS) {
        u32 l2_level_id = ((offset>>12)-L1_BLOCKS) - (idx<<10);
        // dprintf("Hritvik: objstore_read loop 2 %d\n",idx);
        u32 b2r[1024] = {0};
        if(find_read_cached(objfs, obj->l2[idx], (char*)b2r, BLOCK_SIZE)<0) return -1;
        for(int i=l2_level_id;i<1024 && size>0;i++){
            if(find_read_cached(objfs, b2r[i], buf+L1_BLOCKS*BLOCK_SIZE+idx*(BLOCK_SIZE<<10) + i*BLOCK_SIZE, (size>BLOCK_SIZE)?BLOCK_SIZE:size)<0)
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
    // dprintf("Hritvik: %s\n",__func__);
    if(buf->st_ino<2) return -1;
    struct object *obj = load_object(objfs, buf->st_ino);
    if(obj==NULL) return -1;
    // dprintf("Hritvik: %s\n",__func__);
    buf->st_size = obj->size;
    buf->st_blocks = obj->blocks;
    // dprintf("Hritvik: %s\n",__func__);
    free(obj);
    // dprintf("Hritvik: %s\n",__func__);
    return 0;
}

/*
   Set your private pointer, anyway you like.
*/
int objstore_init(struct objfs_state *objfs)
{
    // dprintf("Hritvik: objstore_init\n");
    //lock
    void *ptr;
    malloc_4k(ptr);
    if(!ptr) return -1;
    //TODO: optimize this can directly malloc 4k blocks

    for(u64 i=0;i<BMAP_BLOCKS;i++){
        if(read_block(objfs, (metadata_blocks++), ptr) < 0){
            free_4k(ptr);
            return -1;
        }
        memcpy((char*)(block_bmap + (i<<9)), ptr, BLOCK_SIZE);
    }
    for(u64 i=0;i<BMAP_OBJS;i++){
        if(read_block(objfs, (metadata_blocks++), ptr) < 0){
            free_4k(ptr);
            return -1;
        }
        memcpy((char*)(inode_bmap + (i<<9)), ptr, BLOCK_SIZE);
    }
    //restoring inode to hashTable index mapping
    //2^12/2^2 = 2^10 indices per block
    for(u64 i=0;i<(MAX_OBJS>>10);i++){
        if(read_block(objfs, (metadata_blocks++), (char*)ptr) < 0){
            free_4k(ptr);
            return -1;
        }
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
    // dprintf("Hritvik: %s\n",__func__);
    void *ptr;
    malloc_4k(ptr);
    if(!ptr) return -1;
    // dprintf("Hritvik: %s\n",__func__);
    //TODO: optimize this can directly malloc 4k blocks

    metadata_blocks -= OBJECT_BLOCKS;

    // u32 *hashidx = (u32*)calloc(MAX_OBJS, sizeof(u32));
    u32 hashidx[MAX_OBJS] = {0};
        // dprintf("Hritvik: %s\n",__func__);
    for(u32 i=1;i<HASH_SIZE;i++) {
        // dprintf("Hritvik: %s loop %d\n",__func__,i);
        while(hashTable[i]!=NULL) {
            // dprintf("Hritvik: %s object->id in HT %d i = %d\n",__func__,hashTable[i]->id,i);
            hashidx[hashTable[i]->id] = i;
            hashTable[i] = hashTable[i]->next;
        }
    }
    // dprintf("Hritvik: %s\n",__func__);
    for(int i=MAX_OBJS-1024;i>=0;i-=1024) {
        memcpy(ptr, (char*)(hashidx+i), BLOCK_SIZE);
        if(write_block(objfs, (--metadata_blocks), ptr) < 0){
            free_4k(ptr);
            return -1;
        }
    }
    // free(hashidx);
    // dprintf("Hritvik: %s\n",__func__);
    for(int i=BMAP_OBJS-1;i>=0;i--) {
        memcpy(ptr, (char*)(inode_bmap + (i<<9)), BLOCK_SIZE);
        if(write_block(objfs, (--metadata_blocks), ptr) < 0){
            free_4k(ptr);
            return -1;
        }
    }
    // dprintf("Hritvik: %s block_bmap[0] %lx\n",__func__, block_bmap[0]);
    for(int i=BMAP_BLOCKS-1;i>=0;i--) {
        memcpy(ptr, (char*)(block_bmap + (i<<9)), BLOCK_SIZE);
        if(write_block(objfs, (--metadata_blocks), ptr) < 0){
            free_4k(ptr);
            return -1;
        }
    }

    free_4k(ptr);

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
