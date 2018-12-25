#pragma once
#include "mysql_acl.h"
#include "vector"
#include "ZBase64.h"
#include "hpsocket/hpsocket.h"
#include "rapidjson/writer.h"
#include "rapidjson/document.h"
#include "ConfigRead.h"

//GBase同步库表名
#define GBASE_CK                "ods_rx_ck"             //常口
#define GBASE_LK                "ods_rx_lk"             //流口

//重点库ID
#define ID_CK                160    //常口
#define ID_LK                161    //流口

#define MYSQLUPDATETABLE    "ga_ztryxx"         //备份的重点人员详细信息
#define GAIGNORETABLE       "ga_ignore"         //忽略的编号, 这些编号对应的图片质量太差引起误报
#define MYSQLSTOREFACEINFO  "storefaceinfo"     //入库重点人员信息表
#define MYSQLSTORECOUNT     "storecount"        //布控库数量表
#define MYSQLLAYOUTRESULT   "layoutresult"      //预警结果表

#define JSONSTORELIBID      "StoreLibID"
#define JSONSTOREPHOTO      "Photo"
#define JSONLIBTYPE         "LibType"
#define JSONSTOREFACE       "StoreFace"
#define JSONFACEUUID        "FaceUUID"

#define MAXIMAGESIZE    1024 * 1024 * 10

typedef struct _UpdateDBInfo
{
    char pMysqlDBIP[64];
    int nMysqlDBPort;
    char pMysqlDBName[64];
    char pMysqlDBUser[64];
    char pMysqlDBPassword[64];

    char pUpdateDBName[64];
    char pSavePath[64];
    char pBatchStoreServerIP[64];
    int nBatchStoreServerPort;
}UPDATEDBINFO, *LPUPDATEDBINFO;

typedef struct _ZDRYInfo
{
    char pName[64];
    char pSex[64];
    char pSFZH[64];
    char pAddress[256];
    char pAB[256];              //案别
    char pCrimeAddress[256];    //犯罪地点
    char pComment[1024 * 8];        //备注
    char pZP[256];              //照片URL地址
    char pImagePath[128];       //重点库入库图片本地保存地址
    _ZDRYInfo()
    {
        ZeroMemory(pName, sizeof(pName));
        ZeroMemory(pSex, sizeof(pSex));
        ZeroMemory(pSFZH, sizeof(pSFZH));
        ZeroMemory(pAddress, sizeof(pAddress));
        ZeroMemory(pAB, sizeof(pAB));
        strcpy(pCrimeAddress, "未知");
        ZeroMemory(pComment, sizeof(pComment));
        ZeroMemory(pZP, sizeof(pZP));
        ZeroMemory(pImagePath, sizeof(pImagePath));
    }
}ZDRYINFO, *LPZDRYINFO;
typedef map<string, LPZDRYINFO> MAPZDRYINFO;

class CUpdateLib
{
public:
    CUpdateLib();
    ~CUpdateLib();
public:
    bool StartUpdate();
    bool StopUpdate();
private:
    //同步Oracle黑名单库信息到Mysql
    bool SyncKeyLibInfo();
    //下载新增同步人脸图片到本地保存
    bool DownloadAddImage();
    //新增布控信息到批量布控服务
    bool AddBatchInfo();
    //删除冗余布控信息到批量布控服务
    bool DelBatchInfo();
    //更新库数量到storecount表
    bool UpdateStoreCount();
    //清理所有信息, 为下一次更新准备
    bool ClearInfo();
private:
    bool InsertAddInfoToDB(char * pMsg);
private:
    HANDLE m_hStopEvent;                //停止事件
    CConfigRead * m_pConfigRead;
    CMysql_acl m_mysqltool;             //MySQL主数据库操作(ifrsdb)
    CMysql_acl m_mysqlUpdateDB;         //MySQL同步数据库(testdb)
     
    LPUPDATEDBINFO m_pDBInfo;           //数据库参数信息
    map<int, string> m_mapUpdateLibInfo;//同步黑名单库ID对应表名

    map<string, string> m_mapLayoutBH;  //Mysql黑名单库人员编号, FaceUUID
    set<string> m_setga_ztryxx;         //ga_ztryxx里的编号集合
    set<string> m_setIgnore;            //需要忽略的编号集合
    
    MAPZDRYINFO m_mapZDRYInfo;
    int m_nLayoutSuccess;       
    int m_nLayoutFailed;        //入库失败图片数量

    int m_nCurrentUpdateLibID;  //当前正在更新的重点库ID
    string m_sCurrentImagePath; //当前正在更新的重点库图片保存文件夹path
};

