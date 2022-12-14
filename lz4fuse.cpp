#include <stdint.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <lz4.h>
#include <lz4frame.h>

#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>

/*
void FindNextFrame(int64_t initialindex, std::vector<int64_t>* framelist, FILE* lz4file)
{
    if(lz4file == NULL) // file is not open, so open it
    {
	printf("Error, file is not open...\n");
	//lz4file = fopen(lz4img, "rb");
    }
    else // file is open, continue doing something...
    {
        // get file size
        uint64_t curoffset = 0;
        fseek(lz4file, 0, SEEK_END);
        uint64_t lz4filesize = ftell(lz4file);
        uint32_t header = 0x04224d18;
        printf("lz4file size: %ld\n", lz4filesize);
        rewind(lz4file);
        //while(curoffset < lz4filesize)
        while(curoffset < 131073)
        {
            std::string bufstr;
            //std::string hexstr;
            //std::stringstream ss;
            bufstr.resize(131072);
            fseek(lz4file, curoffset, SEEK_SET);
            //fseek(lz4file, initialindex, SEEK_SET);
            size_t bytesread = fread(const_cast<char*>(bufstr.data()), 1, 131072, lz4file);
            printf("1st 4 bytes of str: %d %d %d %d\n", bufstr[0], bufstr[1], bufstr[2], bufstr[3]);
            // 1st 4 %d of string are what i need, so i may need to sub loop over to find what i need...
            std::size_t found = bufstr.find(std::to_string(header));
            if(found == -1)
                curoffset = curoffset + 131072;
            else
            {
                printf("found: %ld\n", found);
                printf("actual found: %ld\n", found - 3);
                printf("actual file offset: %ld\n", curoffset + found - 3);
                curoffset = curoffset + found + 1;
            }
            printf("curoffset: %ld\n", curoffset);
        }
    }
}
*/

static std::string lz4img;
static std::string lz4mnt;
static std::string ddimg;
static std::vector<int64_t> frameindxlist;
static off_t lz4size = 0;

/*

static QString wfimg;
static QString imgfile;
static QString ifile;
static QString mntpt;
//static QStringList indxlist;
static QList<qint64> frameindxlist;
static const char* relativefilename = NULL;
static const char* rawfilename = NULL;
//static std::string lz4filename;
//static off_t lz4size = 0;
static off_t rawsize = 0;
static size_t framecnt = 0;
//static off_t curoffset = 0;
static off_t blocksize = 0;
*/

static void* lz4_init(struct fuse_conn_info* conn, struct fuse_config* cfg)
{
    (void) conn;
    cfg->kernel_cache = 1;
    return NULL;
}

static int lz4_getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi)
{
    (void) fi;
    int res = 0;
    memset(stbuf, 0, sizeof(struct stat));
    if(strcmp(path, "/") == 0)
    {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    }
    else if(strcmp(path, ddimg.c_str()) == 0)
    {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = 1000;
    }
    else
        res = -ENOENT;

    return res;
}

static int lz4_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags)
{
    (void) offset;
    (void) fi;
    (void) flags;

    if(strcmp(path, "/") != 0)
        return -ENOENT;

    filler(buf, ".", NULL, 0, (fuse_fill_dir_flags)0);
    filler(buf, "..", NULL, 0, (fuse_fill_dir_flags)0);
    filler(buf, ddimg.c_str(), NULL, 0, (fuse_fill_dir_flags)0);

    return 0;
}

static int lz4_open(const char* path, struct fuse_file_info* fi)
{
    if(strcmp(path, ddimg.c_str()) != 0)
        return -ENOENT;

    if((fi->flags & O_ACCMODE) != O_RDONLY)
        return -EACCES;

    return 0;
}

static int lz4_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi)
{
    (void) fi;
    if(strcmp(path, ddimg.c_str()) != 0)
        return -ENOENT;

    FILE* lz4file = NULL;
    lz4file = fopen(lz4img.c_str(), "rb");
    rewind(lz4file);
    uint64_t curoffset = 0;
    uint64_t frameoffset = 0;
    uint64_t framesize = 0;
    LZ4F_dctx* lz4dctx;
    LZ4F_errorCode_t errcode;
    errcode = LZ4F_createDecompressionContext(&lz4dctx, LZ4F_getVersion());
    off_t blksize = 4194304;

    int64_t indxstart = offset / blksize;
    int8_t posodd = offset % blksize;
    int64_t relpos = offset - (indxstart * blksize);
    int64_t indxcnt = size / blksize;
    if(indxcnt == 0)
        indxcnt = 1;
    if(posodd != 0 && (relpos + size) > blksize)
        indxcnt++;
    int64_t indxend = indxstart + indxcnt;
    for(int64_t i=indxstart; i < frameindxlist.size(); i++)
    {
        frameoffset = frameindxlist.at(i);
        if(i == (frameindxlist.size() - 1))
            framesize = framesize - frameoffset;
        else
            framesize = frameindxlist.at(i+1) - frameoffset;
        fseek(lz4file, frameoffset, SEEK_SET);
    }

    fclose(lz4file);
    //fseek(lz4imgfile, 0, SEEK_END);
    //uint64_t lz4filesize = 0;
    //lz4filesize = ftell(lz4imgfile);
    
    return size;

}

/*
static int wombat_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    QDataStream in(&wfi);
    //qint64 lz4start = curoffset;
    char* cmpbuf = new char[2*blocksize];
    QByteArray framearray;
    size_t ret = 1;
    size_t bread = 0;
    size_t rawbufsize = 2*blocksize;
    size_t dstsize = rawbufsize;
    char* rawbuf = new char[rawbufsize];

    //if(indxend > rawsize / blocksize)
    // this should be i=indxstart; i <= indxend; i++)
    //for(int i=indxstart; i < framecnt; i++)
    for(qint64 i=indxstart; i < framecnt; i++)
    {
        frameoffset = frameindxlist.at(i);
        //frameoffset = indxlist.at(i).toULongLong();
        if(i == (framecnt - 1))
            framesize = framesize - 132 - frameoffset;
        else
            framesize = frameindxlist.at(i+1) - frameoffset;
            //framesize = indxlist.at(i+1).toULongLong() - frameoffset;
        wfi.seek(frameoffset);
        //wfi.seek(curoffset + frameoffset);
        int bytesread = in.readRawData(cmpbuf, framesize);
        bread = bytesread;
        ret = LZ4F_decompress(lz4dctx, rawbuf, &dstsize, cmpbuf, &bread, NULL);
        QByteArray blockarray(rawbuf, dstsize);
        framearray.append(blockarray);
    }
    if(posodd == 0)
	memcpy(buf, framearray.mid(0, size).data(), size);
    else
	memcpy(buf, framearray.mid(relpos, size).data(), size);
    
    wfi.close();
    delete[] rawbuf;
    delete[] cmpbuf;
    framearray.clear();

    return size;
}
*/

static void lz4_destroy(void* param)
{
    return;
}

static const struct fuse_operations lz4_oper = {
    .getattr    = lz4_getattr,
    .open       = lz4_open,
    .read       = lz4_read,
    .readdir    = lz4_readdir,
    .init       = lz4_init,
    .destroy    = lz4_destroy,
};

int main(int argc, char* argv[])
{
    //4194304 - default block size 4MB
    //printf("arg count: %d\n", argc);
    if(argc < 3) // no arguments given, display help..
    {
	printf("wrong number of arguments... display help\n");
	return 1;
    }
    lz4img = argv[1];
    lz4mnt = argv[2];
    std::size_t find1 = lz4img.rfind(".");
    std::size_t find2 = lz4img.rfind("/");
    //printf("lz4img path: %s\n", lz4img.c_str());
    //printf("last . is %ld, last / is %ld.\n", find1, find2);
    ddimg = lz4img.substr(find2 + 1, find1 - find2 - 1);
    //printf("dd img file name for fuse: %s\n", ddimg.c_str());
    //printf("substring of lz4 img path %s\n", lz4img.substr(find2 + 1, find1 - find2 - 1).c_str());
    //printf("lz4mnt path: %s\n", lz4mnt.c_str());
    FILE* lz4imgfile = NULL;
    lz4imgfile = fopen(lz4img.c_str(), "rb");
    uint64_t curoffset = 0;
    fseek(lz4imgfile, 0, SEEK_END);
    uint64_t lz4filesize = 0;
    lz4filesize = ftell(lz4imgfile);
    lz4size = lz4filesize;
    //printf("lz4file size: %ld\n", lz4filesize);
    rewind(lz4imgfile);
    uint8_t frameheader[4];
    frameheader[0] = 0x04;
    frameheader[1] = 0x22;
    frameheader[2] = 0x4d;
    frameheader[3] = 0x18;

    while(curoffset < lz4filesize)
    {
        uint8_t* buffer = NULL;
        buffer = (uint8_t*)malloc(sizeof(uint8_t)*4);
        fseek(lz4imgfile, curoffset, SEEK_SET);
        fread(buffer, 1, 4, lz4imgfile);
        //printf("1st 4 bytes of str: %d %d %d %d\n", buffer[0], buffer[1], buffer[2], buffer[3]);
        if(buffer[0] == frameheader[0] && buffer[1] == frameheader[1] && buffer[2] == frameheader[2] && buffer[3] == frameheader[3])
        {
            frameindxlist.push_back(curoffset);
            //printf("curoffset for header match: %ld\n", curoffset);
        }
        curoffset = curoffset + 4;
        free(buffer);
    }
    /*
    for(int i=0; i < frameindxlist.size(); i++)
        printf("frame index value: %d %ld\n", i, frameindxlist.at(i));
    */
    fclose(lz4imgfile);

    char** fargv = NULL;
    fargv = (char**)calloc(2, sizeof(char*));
    int fargc = 2;
    fargv[0] = argv[1];
    fargv[1] = argv[2];
    
    struct fuse_args fuseargs = FUSE_ARGS_INIT(fargc, fargv);

    int ret;
    ret = fuse_main(fuseargs.argc, fuseargs.argv, &lz4_oper, NULL);

    fuse_opt_free_args(&fuseargs);

    return ret;

    //return 0;
}

/*
 *static int
decompress_file_internal(FILE* f_in, FILE* f_out,
                         LZ4F_dctx* dctx,
                         void* src, size_t srcCapacity, size_t filled, size_t alreadyConsumed,
                         void* dst, size_t dstCapacity)
{
    int firstChunk = 1;
    size_t ret = 1;

    assert(f_in != NULL); assert(f_out != NULL);
    assert(dctx != NULL);
    assert(src != NULL); assert(srcCapacity > 0); assert(filled <= srcCapacity); assert(alreadyConsumed <= filled);
    assert(dst != NULL); assert(dstCapacity > 0);

    // Decompression
    while (ret != 0) {
        // Load more input
        size_t readSize = firstChunk ? filled : fread(src, 1, srcCapacity, f_in); firstChunk=0;
        const void* srcPtr = (const char*)src + alreadyConsumed; alreadyConsumed=0;
        const void* const srcEnd = (const char*)srcPtr + readSize;
        if (readSize == 0 || ferror(f_in)) {
            printf("Decompress: not enough input or error reading file\n");
            return 1;
        }

        // Decompress:
         //Continue while there is more input to read (srcPtr != srcEnd)
         //and the frame isn't over (ret != 0)
         
        while (srcPtr < srcEnd && ret != 0) {
            // Any data within dst has been flushed at this stage
            size_t dstSize = dstCapacity;
            size_t srcSize = (const char*)srcEnd - (const char*)srcPtr;
            ret = LZ4F_decompress(dctx, dst, &dstSize, srcPtr, &srcSize, NULL);
            if (LZ4F_isError(ret)) {
                printf("Decompression error: %s\n", LZ4F_getErrorName(ret));
                return 1;
            }
            // Flush output
            if (dstSize != 0) safe_fwrite(dst, 1, dstSize, f_out);
            // Update input
            srcPtr = (const char*)srcPtr + srcSize;
        }

        assert(srcPtr <= srcEnd);

        // Ensure all input data has been consumed.
         // It is valid to have multiple frames in the same file,
         // but this example only supports one frame.
         
        if (srcPtr < srcEnd) {
            printf("Decompress: Trailing data left in file after frame\n");
            return 1;
        }
    }

    // Check that there isn't trailing data in the file after the frame.
     // It is valid to have multiple frames in the same file,
     // but this example only supports one frame.
     //
    {   size_t const readSize = fread(src, 1, 1, f_in);
        if (readSize != 0 || !feof(f_in)) {
            printf("Decompress: Trailing data left in file after frame\n");
            return 1;
    }   }

    return 0;
}


// @return : 1==error, 0==completed
static int
decompress_file_allocDst(FILE* f_in, FILE* f_out,
                        LZ4F_dctx* dctx,
                        void* src, size_t srcCapacity)
{
    assert(f_in != NULL); assert(f_out != NULL);
    assert(dctx != NULL);
    assert(src != NULL);
    assert(srcCapacity >= LZ4F_HEADER_SIZE_MAX);  // ensure LZ4F_getFrameInfo() can read enough data

    // Read Frame header
    size_t const readSize = fread(src, 1, srcCapacity, f_in);
    if (readSize == 0 || ferror(f_in)) {
        printf("Decompress: not enough input or error reading file\n");
        return 1;
    }

    LZ4F_frameInfo_t info;
    size_t consumedSize = readSize;
    {   size_t const fires = LZ4F_getFrameInfo(dctx, &info, src, &consumedSize);
        if (LZ4F_isError(fires)) {
            printf("LZ4F_getFrameInfo error: %s\n", LZ4F_getErrorName(fires));
            return 1;
    }   }

    // Allocating enough space for an entire block isn't necessary for
     // correctness, but it allows some memcpy's to be elided.
     
    size_t const dstCapacity = get_block_size(&info);
    void* const dst = malloc(dstCapacity);
    if (!dst) { perror("decompress_file(dst)"); return 1; }

    int const decompressionResult = decompress_file_internal(
                        f_in, f_out,
                        dctx,
                        src, srcCapacity, readSize-consumedSize, consumedSize,
                        dst, dstCapacity);

    free(dst);
    return decompressionResult;
}


// @result : 1==error, 0==success
static int decompress_file(FILE* f_in, FILE* f_out)
{
    assert(f_in != NULL); assert(f_out != NULL);

    // Resource allocation
    void* const src = malloc(IN_CHUNK_SIZE);
    if (!src) { perror("decompress_file(src)"); return 1; }

    LZ4F_dctx* dctx;
    {   size_t const dctxStatus = LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION);
        if (LZ4F_isError(dctxStatus)) {
            printf("LZ4F_dctx creation error: %s\n", LZ4F_getErrorName(dctxStatus));
    }   }

    int const result = !dctx ? 1 :
                       decompress_file_allocDst(f_in, f_out, dctx, src, IN_CHUNK_SIZE);

    free(src);
    LZ4F_freeDecompressionContext(dctx);   // note : free works on NULL
    return result;
}
 */
