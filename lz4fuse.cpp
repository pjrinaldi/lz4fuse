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

void FindNextFrame(int64_t initialindex, std::vector<int64_t>* framelist, FILE* lz4file)
{
    if(lz4file == NULL) // file is not open, so open it
    {
	printf("Error, file is not open...\n");
	//lz4file = fopen(lz4img, "rb");
    }
    else // file is open, continue doing something...
    {
        std::string bufstr;
        std::string hexstr;
        std::stringstream ss;
        bufstr.resize(131072);
        fseek(lz4file, initialindex, SEEK_SET);
        fread(const_cast<char*>(bufstr.data()), 1, 131072, lz4file);
        for(int i=0; i < 131072; i++)
        {
            ss << std::hex << int(bufstr[i]);
        }
        hexstr = ss.str();
        std::size_t found = hexstr.find("4224d18");
        //std::size_t found = hexstr.find_first_of("04224d18");
        printf("found offset: %ld\n", found);
        /*
        size_t result = 0;
        fseek(lz4file, initialindex, SEEK_SET);
        char* buffer = NULL;
        buffer = (char*)malloc(sizeof(char)*4);
        result = fread(buffer, 1, 4, lz4file);
        if(result != 4)
            exit(1);
        std::stringstream ss;
        for(int i=0; i < 4; i++)
        {
            ss << std::hex << (int)buffer[i];
        }
        std::string mystr = ss.str();
        printf("hex string: %s\n", mystr.c_str());
        if(mystr.compare("4224d18") == 0)
        {
            framelist->push_back(initialindex + 4);
            printf("frame header found.\n");
            FindNextFrame(initialindex+512, framelist, lz4file);
        }
        else
        {
            printf("frame header not found, comparison failed.\n");
        }
        free(buffer);
        */
    }
}

static std::string lz4img;
static std::string lz4mnt;
static std::vector<int64_t> frameindxlist;

/*
 *
 *std::string str ("Please, replace the vowels in this sentence by asterisks.");
  std::size_t found = str.find_first_of("aeiou");
  while (found!=std::string::npos)
  {
    str[found]='*';
    found=str.find_first_of("aeiou",found+1);
  }

  std::cout << str << '\n';

 */

/*
void FindNextFrame(qint64 initialindex, QList<qint64>* framelist, QFile* wfi)
{
    //qDebug() << "initial index:" << initialindex;
    if(!wfi->isOpen())
        wfi->open(QIODevice::ReadOnly);
    wfi->seek(initialindex);
    QByteArray srcharray = wfi->peek(131072);
    int srchindx = srcharray.toHex().indexOf("04224d18");
    if(srchindx == -1)
    {
        //qDebug() << "this should occur after the last frame near the end of the file";
    }
    //int srchindx = srcharray.toHex().indexOf("04224d18", initialindex*2);
    wfi->seek(initialindex + srchindx/2);
    if(qFromBigEndian<qint32>(wfi->peek(4)) == 0x04224d18)
    {
        //qDebug() << "frame found:" << srchindx/2;
        framelist->append(initialindex + srchindx/2);
        FindNextFrame(initialindex + srchindx/2 + 1, framelist, wfi);
    }
    //else
    //    qDebug() << "frame error:" << srchindx/2;
}

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

static void *wombat_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
    (void) conn;
    cfg->kernel_cache = 1;
    return NULL;
}

static int wombat_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{

    (void) fi;
    int res = 0;

    memset(stbuf, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0)
    {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    }
    else if(strcmp(path, relativefilename) == 0)
    {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = rawsize;
    }
    else
        res = -ENOENT;

    return res;
}

static int wombat_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags)
{
    (void) offset;
    (void) fi;
    (void) flags;

    if (strcmp(path, "/") != 0)
            return -ENOENT;

    filler(buf, ".", NULL, 0, (fuse_fill_dir_flags)0);
    filler(buf, "..", NULL, 0, (fuse_fill_dir_flags)0);
    filler(buf, rawfilename, NULL, 0, (fuse_fill_dir_flags)0);

    return 0;
}

static int wombat_open(const char *path, struct fuse_file_info *fi)
{
    if(strcmp(path, relativefilename) != 0)
            return -ENOENT;

    if ((fi->flags & O_ACCMODE) != O_RDONLY)
            return -EACCES;

    return 0;
}

static int wombat_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    (void) fi;
    if(strcmp(path, relativefilename) != 0)
        return -ENOENT;

    QFile wfi(wfimg);
    wfi.open(QIODevice::ReadOnly);
    QDataStream in(&wfi);
    //qint64 lz4start = curoffset;
    LZ4F_dctx* lz4dctx;
    LZ4F_errorCode_t errcode;
    errcode = LZ4F_createDecompressionContext(&lz4dctx, LZ4F_getVersion());
    char* cmpbuf = new char[2*blocksize];
    QByteArray framearray;
    framearray.clear();
    quint64 frameoffset = 0;
    quint64 framesize = 0;
    size_t ret = 1;
    size_t bread = 0;
    size_t rawbufsize = 2*blocksize;
    size_t dstsize = rawbufsize;
    char* rawbuf = new char[rawbufsize];

    qint64 indxstart = offset / blocksize;
    qint8 posodd = offset % blocksize;
    qint64 relpos = offset - (indxstart * blocksize);
    qint64 indxcnt = size / blocksize;
    if(indxcnt == 0)
        indxcnt = 1;
    if(posodd != 0 && (relpos + size) > blocksize)
        indxcnt++;
    qint64 indxend = indxstart + indxcnt;
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

static void wombat_destroy(void* param)
{
    return;
}

static const struct fuse_operations wombat_oper = {
	.getattr	= wombat_getattr,
	.open		= wombat_open,
	.read		= wombat_read,
	.readdir	= wombat_readdir,
	.init           = wombat_init,
        .destroy        = wombat_destroy,
};
*/
int main(int argc, char* argv[])
{
    printf("arg count: %d\n", argc);
    if(argc < 3) // no arguments given, display help..
    {
	printf("wrong number of arguments... display help\n");
	return 1;
    }
    lz4img = argv[1];
    lz4mnt = argv[2];
    printf("lz4img path: %s\n", lz4img.c_str());
    printf("lz4mnt path: %s\n", lz4mnt.c_str());
    FILE* lz4imgfile = NULL;
    lz4imgfile = fopen(lz4img.c_str(), "rb");
    FindNextFrame(0, &frameindxlist, lz4imgfile);
    printf("Initial Start to lz4 fuse mount from wfi code.\n");
    fclose(lz4imgfile);

    return 0;
}

/*
int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("wombatfuse");
    QCoreApplication::setApplicationVersion("0.1");
    QCommandLineParser parser;
    parser.setApplicationDescription("Fuse mount a wombat forensic image to access raw forensic image");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("image", QCoreApplication::translate("main", "Wombat forensic image file name."));
    parser.addPositionalArgument("mountpoint", QCoreApplication::translate("main", "Mount Point."));

    parser.process(app);

    const QStringList args = parser.positionalArguments();

    if(args.count() <= 1)
    {
        printf("No image and/or mountpoint provided.\n");
        parser.showHelp(1);
        //return 1;
    }
    wfimg = args.at(0);
    mntpt = args.at(1);
    imgfile = wfimg.split("/").last().split(".").first() + ".dd";
    ifile = "/" + imgfile;
    relativefilename = ifile.toStdString().c_str();
    rawfilename = imgfile.toStdString().c_str();

    QFile cwfile(wfimg);
    cwfile.open(QIODevice::ReadOnly);
    QDataStream cin(&cwfile);
    
    // HOW TO GET FRAME INDEX LIST OUT OF THE WFI FILE 
    //QList<qint64> frameindxlist;
    frameindxlist.clear();
    FindNextFrame(0, &frameindxlist, &cwfile);
    //qDebug() << "fil count:" << frameindxlist.count();
*/
    /*
    // METHOD TO GET THE SKIPPABLE FRAME INDX CONTENT !!!!!
    cwfile.seek(cwfile.size() - 128 - 1000000);
    QByteArray skiparray = cwfile.read(1000000);
    //int isskiphead = skiparray.lastIndexOf("_*M");
    int isskiphead = skiparray.toHex().lastIndexOf("5f2a4d18");
    //qDebug() << "isskipahead hex:" << skiparray.toHex().lastIndexOf("5f2a4d18");
    //qDebug() << "isskiphead:" << isskiphead << skiparray.mid(isskiphead/2, 4).toHex();
    QString getindx = "";
    if(qFromBigEndian<quint32>(skiparray.mid(isskiphead/2, 4)) == 0x5f2a4d18)
    {
        //qDebug() << "skippable frame containing the index...";
        cwfile.seek(cwfile.size() - 128 - 1000000 + isskiphead/2 + 8);
        cin >> getindx;
    }
    else
    {
        qDebug() << "couldn't find the skippable frame.";
        return 1;
    }
    */
        
    //qDebug() << "getindx:" << getindx;
    
    //indxlist.clear();
    //indxlist = getindx.split(",", Qt::SkipEmptyParts);
    
    //qDebug() << "indxlist:" << indxlist;
    //qDebug() << "indxlist count:" << indxlist.count();
    //qDebug() << "indxlist last:" << indxlist.last() << "indxlist last - 1:" << indxlist.at(indxlist.count() - 2);
    //qDebug() << "framecnt:" << framecnt << 474;

/*
    cwfile.seek(0);


    qint64 header;
    uint8_t version;
    quint16 sectorsize;
    quint32 blksize;
    qint64 totalbytes;
    QString cnum;
    QString evidnum;
    QString examiner;
    QString description;
    cin >> header >> version >> sectorsize >> blksize >> totalbytes >> cnum >> evidnum >> examiner >> description;
    //qDebug() << "current position before for loop:" << cwfile.pos();
    //curoffset = cwfile.pos();
    framecnt = totalbytes / blksize;
    //qDebug() << "framecnt (size):" << framecnt << "framecnt (count):" << frameindxlist.count();
    rawsize = (off_t)totalbytes;
    blocksize = (size_t)blksize;
    //qDebug() << "blocksize:" << blocksize << "framecnt:" << framecnt;
    cwfile.close();
    //qDebug() << "rawsize - lastframeoffset:" << rawsize << "-" << indxlist.at(474) << rawsize - indxlist.at(474).toULongLong();

    char** fargv = NULL;
    fargv = (char**)calloc(2, sizeof(char*));
    int fargc = 2;
    fargv[0] = argv[1];
    fargv[1] = argv[2];
    struct fuse_args fuseargs = FUSE_ARGS_INIT(fargc, fargv);

    int ret;

    //fuse_opt_parse(NULL, NULL, NULL, NULL);
    ret = fuse_main(fuseargs.argc, fuseargs.argv, &wombat_oper, NULL);
    
    fuse_opt_free_args(&fuseargs);

    return ret;
}
*/
