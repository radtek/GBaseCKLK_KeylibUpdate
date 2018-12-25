#pragma once
#include "mysql_acl.h"
#include "vector"
#include "ZBase64.h"
#include "hpsocket/hpsocket.h"
#include "rapidjson/writer.h"
#include "rapidjson/document.h"
#include "ConfigRead.h"

//GBaseͬ�������
#define GBASE_CK                "ods_rx_ck"             //����
#define GBASE_LK                "ods_rx_lk"             //����

//�ص��ID
#define ID_CK                160    //����
#define ID_LK                161    //����

#define MYSQLUPDATETABLE    "ga_ztryxx"         //���ݵ��ص���Ա��ϸ��Ϣ
#define GAIGNORETABLE       "ga_ignore"         //���Եı��, ��Щ��Ŷ�Ӧ��ͼƬ����̫��������
#define MYSQLSTOREFACEINFO  "storefaceinfo"     //����ص���Ա��Ϣ��
#define MYSQLSTORECOUNT     "storecount"        //���ؿ�������
#define MYSQLLAYOUTRESULT   "layoutresult"      //Ԥ�������

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
    char pAB[256];              //����
    char pCrimeAddress[256];    //����ص�
    char pComment[1024 * 8];        //��ע
    char pZP[256];              //��ƬURL��ַ
    char pImagePath[128];       //�ص�����ͼƬ���ر����ַ
    _ZDRYInfo()
    {
        ZeroMemory(pName, sizeof(pName));
        ZeroMemory(pSex, sizeof(pSex));
        ZeroMemory(pSFZH, sizeof(pSFZH));
        ZeroMemory(pAddress, sizeof(pAddress));
        ZeroMemory(pAB, sizeof(pAB));
        strcpy(pCrimeAddress, "δ֪");
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
    //ͬ��Oracle����������Ϣ��Mysql
    bool SyncKeyLibInfo();
    //��������ͬ������ͼƬ�����ر���
    bool DownloadAddImage();
    //����������Ϣ���������ط���
    bool AddBatchInfo();
    //ɾ�����಼����Ϣ���������ط���
    bool DelBatchInfo();
    //���¿�������storecount��
    bool UpdateStoreCount();
    //����������Ϣ, Ϊ��һ�θ���׼��
    bool ClearInfo();
private:
    bool InsertAddInfoToDB(char * pMsg);
private:
    HANDLE m_hStopEvent;                //ֹͣ�¼�
    CConfigRead * m_pConfigRead;
    CMysql_acl m_mysqltool;             //MySQL�����ݿ����(ifrsdb)
    CMysql_acl m_mysqlUpdateDB;         //MySQLͬ�����ݿ�(testdb)
     
    LPUPDATEDBINFO m_pDBInfo;           //���ݿ������Ϣ
    map<int, string> m_mapUpdateLibInfo;//ͬ����������ID��Ӧ����

    map<string, string> m_mapLayoutBH;  //Mysql����������Ա���, FaceUUID
    set<string> m_setga_ztryxx;         //ga_ztryxx��ı�ż���
    set<string> m_setIgnore;            //��Ҫ���Եı�ż���
    
    MAPZDRYINFO m_mapZDRYInfo;
    int m_nLayoutSuccess;       
    int m_nLayoutFailed;        //���ʧ��ͼƬ����

    int m_nCurrentUpdateLibID;  //��ǰ���ڸ��µ��ص��ID
    string m_sCurrentImagePath; //��ǰ���ڸ��µ��ص��ͼƬ�����ļ���path
};

