#include "StdAfx.h"
#include "UpdateLib.h"


CLogRecorder g_LogRecorder;
CUpdateLib::CUpdateLib()
{
    m_pDBInfo = NULL;
    m_hStopEvent = CreateEvent(NULL, true, false, NULL);
    m_nCurrentUpdateLibID = 0;
    m_sCurrentImagePath = "";
}

CUpdateLib::~CUpdateLib()
{
    if (NULL != m_pDBInfo)
    {
        delete m_pDBInfo;
        m_pDBInfo = NULL;
    }
    SetEvent(m_hStopEvent);
    CloseHandle(m_hStopEvent);
}
bool CUpdateLib::StartUpdate()
{
    m_pConfigRead = new CConfigRead;
    string sPath = m_pConfigRead->GetCurrentPath();
    string sConfigPath = sPath + "/Config/GBaseKeylibUpdate_config.properties";
#ifdef _DEBUG
    sConfigPath = "./Config/GBaseKeylibUpdate_config.properties";
#endif
    g_LogRecorder.InitLogger(sConfigPath.c_str(), "GBaseKeylibUpdateLogger", "GBaseKeylibUpdate");

    //��ȡ�����ļ�
    if (!m_pConfigRead->ReadConfig())
    {
        g_LogRecorder.WriteErrorLogEx(__FUNCTION__, "****Error: ��ȡ�����ļ���������!");
        return false;
    }
    else
    {
        g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "��ȡ�����ļ��ɹ�.");
    }

    m_pDBInfo = new UPDATEDBINFO;
    strcpy(m_pDBInfo->pMysqlDBIP, m_pConfigRead->m_sDBIP.c_str());
    strcpy(m_pDBInfo->pMysqlDBName, m_pConfigRead->m_sDBName.c_str());
    strcpy(m_pDBInfo->pMysqlDBUser, m_pConfigRead->m_sDBUid.c_str());
    strcpy(m_pDBInfo->pMysqlDBPassword, m_pConfigRead->m_sDBPwd.c_str());
    m_pDBInfo->nMysqlDBPort = m_pConfigRead->m_nDBPort;

    strcpy(m_pDBInfo->pUpdateDBName, m_pConfigRead->m_sUpdateDBName.c_str());
    strcpy(m_pDBInfo->pSavePath, m_pConfigRead->m_sSavePath.c_str());
    strcpy(m_pDBInfo->pBatchStoreServerIP, m_pConfigRead->m_sBatchStoreServerIP.c_str());
    m_pDBInfo->nBatchStoreServerPort = m_pConfigRead->m_nBatchStoreServerPort;

    m_mapUpdateLibInfo.insert(make_pair(ID_CK, GBASE_CK));
    m_mapUpdateLibInfo.insert(make_pair(ID_LK, GBASE_LK));

    int nDay = 0;
#ifndef _DEBUG  
    {
        time_t t = time(NULL);
        tm * tCurrent = localtime(&t);
        nDay = tCurrent->tm_mday;
    }
#endif
    
    do 
    {
        time_t t = time(NULL);
        tm * tCurrent = localtime(&t);
        if(nDay != tCurrent->tm_mday)
        {
            nDay = tCurrent->tm_mday;
        }
        else
        {
            continue;
        }

        //����MySQL���ݿ�
        if (!m_mysqltool.mysql_connectDB(m_pDBInfo->pMysqlDBIP, m_pDBInfo->nMysqlDBPort, m_pDBInfo->pMysqlDBName,
            m_pDBInfo->pMysqlDBUser, m_pDBInfo->pMysqlDBPassword, "gb2312"))
        {
            g_LogRecorder.WriteErrorLogEx(__FUNCTION__, "****Error: ����Mysql���ݿ�ʧ��[%s:%d:%s]!",
                m_pDBInfo->pMysqlDBIP, m_pDBInfo->nMysqlDBPort, m_pDBInfo->pMysqlDBName);
            return false;
        }
        else
        {
            g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "����Mysql���ݿ�ɹ�[%s:%d:%s].",
                m_pDBInfo->pMysqlDBIP, m_pDBInfo->nMysqlDBPort, m_pDBInfo->pMysqlDBName);
        }

        //����MySQLͬ�����ݿ�
        if (!m_mysqlUpdateDB.mysql_connectDB(m_pDBInfo->pMysqlDBIP, m_pDBInfo->nMysqlDBPort, m_pDBInfo->pUpdateDBName,
            m_pDBInfo->pMysqlDBUser, m_pDBInfo->pMysqlDBPassword, "gb2312"))
        {
            g_LogRecorder.WriteErrorLogEx(__FUNCTION__, "****Error: ����Mysql���ݿ�ʧ��[%s:%d:%s]!",
                m_pDBInfo->pMysqlDBIP, m_pDBInfo->nMysqlDBPort, m_pDBInfo->pUpdateDBName);
            return false;
        }
        else
        {
            g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "����Mysql���ݿ�ɹ�[%s:%d:%s].",
                m_pDBInfo->pMysqlDBIP, m_pDBInfo->nMysqlDBPort, m_pDBInfo->pUpdateDBName);
        }

        map<int, string>::iterator it = m_mapUpdateLibInfo.begin();
        for (; it != m_mapUpdateLibInfo.end(); it++)
        {
            m_nCurrentUpdateLibID = it->first;
            //��ͬ�����ݿ�ȡ����������Ϣ���µ�Mysql�����ݿ�
            if (!SyncKeyLibInfo())
            {
                g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "Error : ͬ������������Ϣʧ��!");
                return false;
            }
            //��������ͼƬ�����ر���
            if (!DownloadAddImage())
            {
                g_LogRecorder.WriteDebugLog(__FUNCTION__, "Error: ��������ͼƬ���浽����ʧ��!");
                return false;
            }
            //������ͼƬ��Ϣ���͵��������ط���
            if (!AddBatchInfo())
            {
                return false;
            }
            //ɾ�����಼����Ϣ���������ط���
            if (!DelBatchInfo())
            {
                return false;
            }
            //����StoreCount�����ͼƬ����
            if (!UpdateStoreCount())
            {
                return false;
            }
            //������Ϣ, Ϊ��һ�θ���׼��
            if (!ClearInfo())
            {
                return false;
            }
        }

        m_setIgnore.clear();
        m_setga_ztryxx.clear();
        m_mysqltool.mysql_disconnectDB();
        g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "���½���, �ȴ���һ�θ���.....");
    } while (WAIT_TIMEOUT == WaitForSingleObject(m_hStopEvent, 1000 * 60 * 30));
    
    return true;
}
bool CUpdateLib::StopUpdate()
{
    ClearInfo();
    SetEvent(m_hStopEvent);
    return true;
}
//����������Ϣ, Ϊ��һ�θ���׼��
bool CUpdateLib::ClearInfo()
{
    m_nLayoutSuccess = 0;
    m_nLayoutFailed = 0;
    m_mapLayoutBH.clear();
    MAPZDRYINFO::iterator it = m_mapZDRYInfo.begin();
    while(it != m_mapZDRYInfo.end())
    {
        remove(it->second->pImagePath);
        delete it->second;
        it = m_mapZDRYInfo.erase(it);
    }
    if ("" != m_sCurrentImagePath)
    {
        if (RemoveDirectory(m_sCurrentImagePath.c_str()))
        {
            g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "ɾ���ļ���[%s]�ɹ�!", m_sCurrentImagePath.c_str());
        }
        else
        {
            g_LogRecorder.WriteWarnLogEx(__FUNCTION__, "***Warning: ɾ���ļ���[%s]ʧ��!", m_sCurrentImagePath.c_str());
        }
    }
    m_sCurrentImagePath = "";
    return true;
}
//ͬ��GBase����������Ϣ��Mysql
bool CUpdateLib::SyncKeyLibInfo()
{
    g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "\n***************��ʼͬ����������[%d][%s]��Ϣ***************", 
        m_nCurrentUpdateLibID, m_mapUpdateLibInfo[m_nCurrentUpdateLibID].c_str());
    int nRet = 0;
    int nNumber = 0;
    char pSql[1024 * 8] = { 0 };
    char pZDRYBH[64] = { 0 };
    char pFaceUUID[64] = { 0 };

    //��ȡga_ztryxx������Ϣ�����ڲ���ʱ�ж��Ƿ���Ҫ����
    if (m_setga_ztryxx.size() == 0)
    {
        sprintf_s(pSql, sizeof(pSql),
            "Select ztrybh from %s", MYSQLUPDATETABLE);
        nRet = m_mysqltool.mysql_exec(_MYSQL_SELECT, pSql);
        if (nRet > 0)
        {
            while (m_mysqltool.mysql_getNextRow())
            {
                strcpy_s(pZDRYBH, sizeof(pZDRYBH), m_mysqltool.mysql_getRowStringValue("ztrybh"));
                m_setga_ztryxx.insert(pZDRYBH);
            }
            printf("Get Count[%d] from %s..\n", m_setga_ztryxx.size(), MYSQLUPDATETABLE);
        }
    }
    else
    {
        printf("%s�����ݼ���ȡ, Count[%d].", MYSQLUPDATETABLE, m_setga_ztryxx.size());
    }

    //��ȡga_ignore������Ϣ�����ڲ���ʱ�ж��Ƿ���Ҫ����
    if (m_setIgnore.size() == 0)
    {
        sprintf_s(pSql, sizeof(pSql),
            "Select ztrybh from %s", GAIGNORETABLE);
        nRet = m_mysqltool.mysql_exec(_MYSQL_SELECT, pSql);
        if (nRet > 0)
        {
            while (m_mysqltool.mysql_getNextRow())
            {
                strcpy_s(pZDRYBH, sizeof(pZDRYBH), m_mysqltool.mysql_getRowStringValue("ztrybh"));
                m_setIgnore.insert(pZDRYBH);
            }
            printf("Get Count[%d] from %s..\n", m_setIgnore.size(), GAIGNORETABLE);
        }
    }
    else
    {
        printf("%s�����ݼ���ȡ, Count[%d].\n", GAIGNORETABLE, m_setIgnore.size());
    }
    

    //��ȡmysql storefaceinfo�������ص���Ա��Ϣ, ���ں���ĸ��»����;
    g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "��ȡMysql���ݿ����������Ա��Ϣ...");
    sprintf_s(pSql, sizeof(pSql),
        "Select zdrybh, faceuuid from %s where LayoutLibId = %d and zdrybh is not null", MYSQLSTOREFACEINFO, m_nCurrentUpdateLibID);
    nRet = m_mysqltool.mysql_exec(_MYSQL_SELECT, pSql);
    if (nRet > 0)
    {
        while (m_mysqltool.mysql_getNextRow())
        {
            strcpy_s(pZDRYBH, sizeof(pZDRYBH), m_mysqltool.mysql_getRowStringValue("zdrybh"));
            strcpy_s(pFaceUUID, sizeof(pFaceUUID), m_mysqltool.mysql_getRowStringValue("faceuuid"));
            //�����������ڲ��ص���Ա�����Ϣ, �����ж��Ƿ���Ҫɾ�����������
            m_mapLayoutBH.insert(make_pair(pZDRYBH, pFaceUUID));
        }
    }
    g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "��ȡMysql���ݿ��������[%d]��Ա�����Ϣ����, ��Ա����[%d].", m_nCurrentUpdateLibID, m_mapLayoutBH.size());

    //��������ͼƬ�ļ��ӵ�ַ
    SYSTEMTIME sysTime;
    GetLocalTime(&sysTime);
    char pTimeID[20] = { 0 };
    sprintf_s(pTimeID, sizeof(pTimeID), "%d-%04d%02d%02d%02d%02d%02d/",
        m_nCurrentUpdateLibID, sysTime.wYear, sysTime.wMonth, sysTime.wDay, sysTime.wHour, sysTime.wMinute, sysTime.wSecond);
    string sKeylibPath = m_pDBInfo->pSavePath;
    m_sCurrentImagePath = m_pDBInfo->pSavePath + string(pTimeID);
    CreateDirectory(sKeylibPath.c_str(), NULL);
    CreateDirectory(m_sCurrentImagePath.c_str(), NULL);
    g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "�����ļ���[%s]�ɹ�.", m_sCurrentImagePath.c_str());


    //��ȡͬ�����ݿ�����������Ա��Ϣ, û����Ա��ŵ�, �����֤��Ϊ��Ա���
    switch (m_nCurrentUpdateLibID)
    {
        case ID_QGWF_PQ: case ID_QGWF_RSDQ: //ȫ��Υ��_����, ȫ��Υ��_���ҵ���
        {
            sprintf_s(pSql, sizeof(pSql),
                "select XM, XB, SFZH, JZD, MZ, CSRQ, SG, HJD, ODS_INPUT_TIME, ZP "
                "from %s where SFZH is not NULL and ZP is not NULL group by SFZH", 
                m_mapUpdateLibInfo[m_nCurrentUpdateLibID].c_str());
            break;
        } 
        case ID_QGWF_PQLCYS: case ID_QGWF_RSDQLCYS: //ȫ��Υ��_���ΰ�������, ȫ��Υ��_���ҵ�����������
        {
            sprintf_s(pSql, sizeof(pSql),
                "select XM, XB, SFZH, JZD, MZ, CSRQ, SG, HJD, ODS_INPUT_TIME, XLAB, JYAQ, ZP "
                "from %s where SFZH is not NULL and ZP is not NULL group by SFZH",
                m_mapUpdateLibInfo[m_nCurrentUpdateLibID].c_str());
            break;
        }
        case ID_ZFBA_DQCNCW: case ID_ZFBA_DQDDC: case ID_ZFBA_DQDDCDP: case ID_ZFBA_DQJQZP: case ID_ZFBA_FMDP: case ID_ZFBA_XD:
        {
            sprintf_s(pSql, sizeof(pSql),
                "select RYBH, XM, XB, ZJHM, JZD, AB, MZ, CSRQ, SG, HJDQH, HJDXZ, LRDW, YWGXBM, XZCS, LRSJ, WFJL, ZP "
                "from %s where RYBH is not NULL and ZP is not NULL group by RYBH", 
                m_mapUpdateLibInfo[m_nCurrentUpdateLibID].c_str());
            break;
        }
        case ID_ZFBA_PQ: case ID_ZFBA_RSDQ:
        {
            sprintf_s(pSql, sizeof(pSql),
                "select RYBH, XM, XB, ZJHM, JZD, AB, ABXL, MZ, CSRQ, SG, HJDQH, HJDXZ, LRDW, YWGXBM, XZCS, LRSJ, WFJL, ZP "
                "from %s where RYBH is not NULL and ZP is not NULL group by RYBH",
                m_mapUpdateLibInfo[m_nCurrentUpdateLibID].c_str());
            break;
        }
        case ID_ZTRYCX_PQ: case ID_ZTRYCX_RSDQ:
        {
            sprintf_s(pSql, sizeof(pSql),
                "select RYBH, XM, XB, ZJHM, HJDQH, AB, JYAQ, LADW, ZHDW, RBDJKRQ, ZP "
                "from %s where RYBH is not NULL and ZP is not NULL group by RYBH",
                m_mapUpdateLibInfo[m_nCurrentUpdateLibID].c_str());
            break;
        }
        case ID_CK: case ID_LK:
        {
            sprintf_s(pSql, sizeof(pSql),
                "select xm, jg, hjd, zp "
                "from %s where zp is not null",
                m_mapUpdateLibInfo[m_nCurrentUpdateLibID].c_str());
            break;
        }
    }
    
    nRet = m_mysqlUpdateDB.mysql_exec(_MYSQL_SELECT, pSql);
    if (nRet < 0)
    {
        g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "ִ��SQL���ʧ��!");
        return false;
    }
    else
    {
        g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "ִ��SQL���, ��ȡ%s����������Ա��Ϣ��...!", m_mapUpdateLibInfo[m_nCurrentUpdateLibID].c_str());
    }
    
    char RYBH[256] = { 0 };     //��Ա���
    char XM[256] = { 0 };       //����
    char XB[256] = { 0 };       //�Ա�
    char ZJHM[256] = { 0 };     //֤������
    char JZD[256] = { 0 };      //��ס��
    char AB[256] = { 0 };       //����
    char ABXL[256] = { 0 };     //����ϸ��
    char MZ[256] = { 0 };       //����
    char CSRQ[256] = { 0 };     //��������  
    char SG[256] = { 0 };       //���
    char HJDQH[256] = { 0 };    //����������
    char HJDXZ[256] = { 0 };    //��������ϸסַ
    char YWGXBM[256] = { 0 };   //ҵ���Ͻ����
    char LRDW[2014] = { 0 };    //¼�뵥λ
    char LRSJ[256] = { 0 };     //¼��ʱ��
    char WFJL[1024 * 8] = { 0 };//Υ����¼
    char XZCS[1024] = { 0 };    //ѡ����
    char BM[256] = { 0 };       //����(�º�)
    char ZHDW[256] = { 0 };     //ץ��λ
    char ZP[1024] = { 0 };      //��Ƭ(URL)

    int nAdd = 0;
    char pComment[1024 * 8] = { 0 };    //��ע
    while (m_mysqlUpdateDB.mysql_getNextRow())
    {
        switch (m_nCurrentUpdateLibID)
        {
            case ID_QGWF_PQ: case ID_QGWF_RSDQ:
            {
                strcpy_s(RYBH,  sizeof(RYBH),   m_mysqlUpdateDB.mysql_getRowStringValue("SFZH"));
                strcpy_s(XM,    sizeof(XM),     m_mysqlUpdateDB.mysql_getRowStringValue("XM"));
                strcpy_s(XB,    sizeof(XB),     m_mysqlUpdateDB.mysql_getRowStringValue("XB"));
                strcpy_s(ZJHM,  sizeof(ZJHM),   m_mysqlUpdateDB.mysql_getRowStringValue("SFZH"));
                strcpy_s(JZD,   sizeof(JZD),    m_mysqlUpdateDB.mysql_getRowStringValue("JZD"));
                if (ID_QGWF_PQ == m_nCurrentUpdateLibID)    strcpy_s(AB, sizeof(AB), "ȫ��Υ������");
                else strcpy_s(AB, sizeof(AB),   "ȫ��Υ�����ҵ���");
                strcpy_s(MZ,    sizeof(MZ),     m_mysqlUpdateDB.mysql_getRowStringValue("MZ"));
                strcpy_s(CSRQ,  sizeof(CSRQ),   m_mysqlUpdateDB.mysql_getRowStringValue("CSRQ"));
                strcpy_s(SG,    sizeof(SG),     m_mysqlUpdateDB.mysql_getRowStringValue("SG"));
                strcpy_s(HJDQH, sizeof(HJDQH),  "null");
                strcpy_s(HJDXZ, sizeof(HJDXZ),  m_mysqlUpdateDB.mysql_getRowStringValue("HJD"));
                strcpy_s(LRSJ,  sizeof(LRSJ),   m_mysqlUpdateDB.mysql_getRowStringValue("ODS_INPUT_TIME"));
                strcpy_s(XZCS,  sizeof(XZCS),   "δ֪");
                strcpy_s(WFJL,  sizeof(WFJL),   "null");
                strcpy_s(ZP,    sizeof(ZP),     m_mysqlUpdateDB.mysql_getRowStringValue("ZP"));
                //test
                //strcpy_s(ZP, sizeof(ZP), "http://35.24.22.179/D/StoreSTLibServer/106/4637442370A94a8d920416C46591D1DA.jpg");
                sprintf_s(pComment, sizeof(pComment), "����: %s; \n¼��ʱ��: %s; \nΥ����¼: %s", AB, LRSJ, WFJL);
                break;
            }
            case ID_QGWF_PQLCYS: case ID_QGWF_RSDQLCYS:
            {
                strcpy_s(RYBH,  sizeof(RYBH),   m_mysqlUpdateDB.mysql_getRowStringValue("SFZH"));
                strcpy_s(XM,    sizeof(XM),     m_mysqlUpdateDB.mysql_getRowStringValue("XM"));
                strcpy_s(XB,    sizeof(XB),     m_mysqlUpdateDB.mysql_getRowStringValue("XB"));
                strcpy_s(ZJHM,  sizeof(ZJHM),   m_mysqlUpdateDB.mysql_getRowStringValue("SFZH"));
                strcpy_s(JZD,   sizeof(JZD),    m_mysqlUpdateDB.mysql_getRowStringValue("JZD"));
                strcpy_s(AB,    sizeof(AB),     m_mysqlUpdateDB.mysql_getRowStringValue("XLAB"));
                strcpy_s(MZ,    sizeof(MZ),     m_mysqlUpdateDB.mysql_getRowStringValue("MZ"));
                strcpy_s(CSRQ,  sizeof(CSRQ),   m_mysqlUpdateDB.mysql_getRowStringValue("CSRQ"));
                strcpy_s(SG,    sizeof(SG),     m_mysqlUpdateDB.mysql_getRowStringValue("SG"));
                strcpy_s(HJDQH, sizeof(HJDQH),  "null");
                strcpy_s(HJDXZ, sizeof(HJDXZ),  m_mysqlUpdateDB.mysql_getRowStringValue("HJD"));
                strcpy_s(LRSJ,  sizeof(LRSJ),   m_mysqlUpdateDB.mysql_getRowStringValue("ODS_INPUT_TIME"));
                strcpy_s(XZCS,  sizeof(XZCS),   "δ֪");
                strcpy_s(WFJL,  sizeof(WFJL), m_mysqlUpdateDB.mysql_getRowStringValue("JYAQ"));
                strcpy_s(ZP,    sizeof(ZP),     m_mysqlUpdateDB.mysql_getRowStringValue("ZP"));
                sprintf_s(pComment, sizeof(pComment), "����: %s; \n¼��ʱ��: %s; \nΥ����¼: %s", AB, LRSJ, WFJL);
                break;
            }
            case ID_ZFBA_DQCNCW: case ID_ZFBA_DQDDC: case ID_ZFBA_DQDDCDP: case ID_ZFBA_DQJQZP: case ID_ZFBA_FMDP: case ID_ZFBA_XD:
            {
                strcpy_s(RYBH,  sizeof(RYBH),   m_mysqlUpdateDB.mysql_getRowStringValue("RYBH"));
                strcpy_s(XM,    sizeof(XM),     m_mysqlUpdateDB.mysql_getRowStringValue("XM"));
                strcpy_s(XB,    sizeof(XB),     m_mysqlUpdateDB.mysql_getRowStringValue("XB"));
                strcpy_s(ZJHM,  sizeof(ZJHM),   m_mysqlUpdateDB.mysql_getRowStringValue("ZJHM"));
                strcpy_s(JZD,   sizeof(JZD),    m_mysqlUpdateDB.mysql_getRowStringValue("JZD"));
                strcpy_s(AB,    sizeof(AB),     m_mysqlUpdateDB.mysql_getRowStringValue("AB"));
                strcpy_s(MZ,    sizeof(MZ),     m_mysqlUpdateDB.mysql_getRowStringValue("MZ"));
                strcpy_s(CSRQ,  sizeof(CSRQ),   m_mysqlUpdateDB.mysql_getRowStringValue("CSRQ"));
                strcpy_s(SG,    sizeof(SG),     m_mysqlUpdateDB.mysql_getRowStringValue("SG"));
                strcpy_s(HJDQH, sizeof(HJDQH),  m_mysqlUpdateDB.mysql_getRowStringValue("HJDQH"));
                strcpy_s(HJDXZ, sizeof(HJDXZ),  m_mysqlUpdateDB.mysql_getRowStringValue("HJDXZ"));
                strcpy_s(YWGXBM, sizeof(YWGXBM), m_mysqlUpdateDB.mysql_getRowStringValue("YWGXBM"));
                strcpy_s(LRDW, sizeof(LRDW),    m_mysqlUpdateDB.mysql_getRowStringValue("LRDW"));
                strcpy_s(LRSJ, sizeof(LRSJ),    m_mysqlUpdateDB.mysql_getRowStringValue("LRSJ"));
                strcpy_s(XZCS, sizeof(XZCS),    m_mysqlUpdateDB.mysql_getRowStringValue("XZCS"));
                strcpy_s(WFJL,  sizeof(WFJL),   m_mysqlUpdateDB.mysql_getRowStringValue("WFJL"));
                strcpy_s(ZP,    sizeof(ZP),     m_mysqlUpdateDB.mysql_getRowStringValue("ZP"));
                sprintf_s(pComment, sizeof(pComment),
                    "����: %s; \nҵ���Ͻ����: %s;\n¼�뵥λ: %s; \n¼��ʱ��: %s; \nΥ����¼: %s",
                    AB, YWGXBM, LRDW, LRSJ, WFJL);
                break;
            }
            case ID_ZFBA_PQ: case ID_ZFBA_RSDQ:
            {
                strcpy_s(RYBH,  sizeof(RYBH),   m_mysqlUpdateDB.mysql_getRowStringValue("RYBH"));
                strcpy_s(XM,    sizeof(XM),     m_mysqlUpdateDB.mysql_getRowStringValue("XM"));
                strcpy_s(XB,    sizeof(XB),     m_mysqlUpdateDB.mysql_getRowStringValue("XB"));
                strcpy_s(ZJHM,  sizeof(ZJHM),   m_mysqlUpdateDB.mysql_getRowStringValue("ZJHM"));
                strcpy_s(JZD,   sizeof(JZD),    m_mysqlUpdateDB.mysql_getRowStringValue("JZD"));
                strcpy_s(AB,    sizeof(AB),     m_mysqlUpdateDB.mysql_getRowStringValue("AB"));
                strcpy_s(ABXL,  sizeof(ABXL),   m_mysqlUpdateDB.mysql_getRowStringValue("ABXL"));
                strcpy_s(MZ,    sizeof(MZ),     m_mysqlUpdateDB.mysql_getRowStringValue("MZ"));
                strcpy_s(CSRQ,  sizeof(CSRQ),   m_mysqlUpdateDB.mysql_getRowStringValue("CSRQ"));
                strcpy_s(SG,    sizeof(SG),     m_mysqlUpdateDB.mysql_getRowStringValue("SG"));
                strcpy_s(HJDQH, sizeof(HJDQH),  m_mysqlUpdateDB.mysql_getRowStringValue("HJDQH"));
                strcpy_s(HJDXZ, sizeof(HJDXZ),  m_mysqlUpdateDB.mysql_getRowStringValue("HJDXZ"));
                strcpy_s(YWGXBM, sizeof(YWGXBM), m_mysqlUpdateDB.mysql_getRowStringValue("YWGXBM"));
                strcpy_s(LRDW,  sizeof(LRDW),   m_mysqlUpdateDB.mysql_getRowStringValue("LRDW"));
                strcpy_s(LRSJ,  sizeof(LRSJ),   m_mysqlUpdateDB.mysql_getRowStringValue("LRSJ"));
                strcpy_s(XZCS,  sizeof(XZCS),   m_mysqlUpdateDB.mysql_getRowStringValue("XZCS"));
                strcpy_s(WFJL,  sizeof(WFJL),   m_mysqlUpdateDB.mysql_getRowStringValue("WFJL"));
                strcpy_s(ZP,    sizeof(ZP),     m_mysqlUpdateDB.mysql_getRowStringValue("ZP"));
                sprintf_s(pComment, sizeof(pComment),
                    "����: %s; \n����ϸ��: %s; \nҵ���Ͻ����: %s;\n¼�뵥λ: %s; \n¼��ʱ��: %s; \nΥ����¼: %s",
                    AB, ABXL, YWGXBM, LRDW, LRSJ, WFJL);
                break;
            }
            case ID_ZTRYCX_PQ: case ID_ZTRYCX_RSDQ:
            {
                strcpy_s(RYBH,  sizeof(RYBH),   m_mysqlUpdateDB.mysql_getRowStringValue("RYBH"));
                strcpy_s(XM,    sizeof(XM),     m_mysqlUpdateDB.mysql_getRowStringValue("XM"));
                strcpy_s(XB,    sizeof(XB),     m_mysqlUpdateDB.mysql_getRowStringValue("XB"));
                strcpy_s(ZJHM,  sizeof(ZJHM),   m_mysqlUpdateDB.mysql_getRowStringValue("ZJHM"));
                strcpy_s(JZD,   sizeof(JZD),    "δ֪");
                strcpy_s(AB,    sizeof(AB),     m_mysqlUpdateDB.mysql_getRowStringValue("AB"));
                strcpy_s(MZ,    sizeof(MZ),     "δ֪");
                strcpy_s(CSRQ,  sizeof(CSRQ),   "δ֪");
                strcpy_s(SG,    sizeof(SG),     "δ֪");
                strcpy_s(HJDQH, sizeof(HJDQH),  m_mysqlUpdateDB.mysql_getRowStringValue("HJDQH"));
                strcpy_s(HJDXZ, sizeof(HJDXZ),  "δ֪");
                strcpy_s(LRDW, sizeof(LRDW),    m_mysqlUpdateDB.mysql_getRowStringValue("LADW"));
                strcpy_s(LRSJ, sizeof(LRSJ),    m_mysqlUpdateDB.mysql_getRowStringValue("RBDJKRQ"));
                strcpy_s(XZCS, sizeof(XZCS),    "δ֪");
                strcpy_s(ZHDW,  sizeof(ZHDW),   m_mysqlUpdateDB.mysql_getRowStringValue("ZHDW"));
                strcpy_s(WFJL,  sizeof(WFJL),   m_mysqlUpdateDB.mysql_getRowStringValue("JYAQ"));
                strcpy_s(ZP,    sizeof(ZP),     m_mysqlUpdateDB.mysql_getRowStringValue("ZP"));
                sprintf_s(pComment, sizeof(pComment),
                    "����: %s; \n������λ: %s; \nץ��λ: %s; \nΥ����¼: %s",
                    AB, LRDW, ZHDW, WFJL);
                break;
            }
            case ID_CK: case ID_LK:
            {
                strcpy_s(XM, sizeof(XM), m_mysqlUpdateDB.mysql_getRowStringValue("xm"));
                strcpy_s(HJDQH, sizeof(HJDQH), m_mysqlUpdateDB.mysql_getRowStringValue("jg"));
                strcpy_s(HJDXZ, sizeof(HJDXZ), m_mysqlUpdateDB.mysql_getRowStringValue("hjd"));
                strcpy_s(ZP, sizeof(ZP), m_mysqlUpdateDB.mysql_getRowStringValue("zp"));
            }
        }
        

        map<string, string>::iterator it = m_mapLayoutBH.find(RYBH);
        set<string>::iterator itJudeg = m_setga_ztryxx.begin();
        //���֤��ż����������, δ���������Mysqlͬ����
        if (it != m_mapLayoutBH.end())
        {
            m_mapLayoutBH.erase(it);
        }
        else
        {
            set<string>::iterator itIgnore = m_setIgnore.find(RYBH);  //����ȷ���˱���Ƿ���Ҫ����
            if (itIgnore != m_setIgnore.end())
            {
                printf("Ignore BH[%s].\n", RYBH);
            }
            else
            {
                nAdd++;
                LPZDRYINFO pZDRYInfo = new ZDRYINFO;
                strcpy(pZDRYInfo->pName, XM);
                if (string(XB) == "Ů")
                {
                    strcpy(pZDRYInfo->pSex, "2");
                }
                else
                {
                    strcpy(pZDRYInfo->pSex, "1");
                }
                strcpy(pZDRYInfo->pSFZH, ZJHM);
                strcpy(pZDRYInfo->pAddress, JZD);
                strcpy(pZDRYInfo->pAB, AB);
                strcpy(pZDRYInfo->pCrimeAddress, XZCS);
                if(string(pComment).find("'") != string::npos)
                {
                    printf("Comment: %s\n", pComment);
                    string sComment(pComment);
                    size_t nPos = sComment.find("'");
                    if(string::npos != nPos)
                    {
                        sComment.erase(nPos, 1);
                        strcpy(pZDRYInfo->pComment, sComment.c_str());
                        printf("Comment: %s\n", pZDRYInfo->pComment);
                    }
                }
                else
                {
                    strcpy(pZDRYInfo->pComment, pComment);
                }
                strcpy(pZDRYInfo->pZP, ZP);
                string sTempPath = m_sCurrentImagePath + string(RYBH) + string(".jpg");
                strcpy(pZDRYInfo->pImagePath, sTempPath.c_str());
                m_mapZDRYInfo.insert(make_pair(RYBH, pZDRYInfo));


                itJudeg = m_setga_ztryxx.find(RYBH);  //�ж���ga_ztryxx���Ƿ񼺴�����������, �����������
                if (itJudeg == m_setga_ztryxx.end())
                {
                    sprintf_s(pSql, sizeof(pSql),
                        "INSERT INTO %s(ZTRYBH,XM,XBDM_CN,SFZH,SFZ18,XZZ_DZMC,ZTRYLXDM_CN,MZDM_CN,CSRQ,SGXX,SGSX, "
                        "HJDZ_XZQHDM,HJDZ_DZMC,JL_GXSJ,JL_RKSJ) "
                        "VALUES ('%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s')",
                        MYSQLUPDATETABLE,
                        RYBH, XM, XB, ZJHM, ZJHM, JZD, AB, MZ, CSRQ, SG, SG, HJDQH, HJDXZ, LRSJ, LRSJ);
                    nRet = m_mysqltool.mysql_exec(_MYSQL_INSERT, pSql);
                    if (nRet < 0)
                    {
                        g_LogRecorder.WriteWarnLogEx(__FUNCTION__, "***Warning: ͬ�����ݿ�ͬ���ص���Ա��Ϣ��Mysql��ʧ��.");
                    }
                }
            }
        }

        nNumber++;
        if (nNumber % 2000 == 0)
        {
            g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "��ͬ����Ա��Ϣ: %d", nNumber);
        }
    }

    g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "ͬ������������Ա��Ϣ�����, ͬ��[%d], Add[%d].", nNumber, nAdd);
    return true;
}
//��������ͬ������ͼƬ�����ر���
bool CUpdateLib::DownloadAddImage()
{
    g_LogRecorder.WriteDebugLog(__FUNCTION__, "��ʼ������������ͼƬ...");
   
    DWORD dwWrite = 0;
    int nLen = 0;
    string sTempPath = "";

    CHttpSyncClientPtr pClient(NULL);
    char pSql[1024 * 8] = { 0 };
    int nAdd = 0;
    MAPZDRYINFO::iterator it = m_mapZDRYInfo.begin();
    while (it != m_mapZDRYInfo.end())
    {
        //ȡͼƬ
        pClient->OpenUrl("GET", it->second->pZP);

        //��ȡ��Ӧ��Ϣ
        LPCBYTE pRespon = NULL;
        int nRepSize = 0;
        pClient->GetResponseBody(&pRespon, &nRepSize);

        if(nRepSize > 0)
        {
            HANDLE hFileHandle = CreateFile(it->second->pImagePath,
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL,
                NULL);

            WriteFile(hFileHandle, pRespon, nRepSize, &dwWrite, NULL);
            CloseHandle(hFileHandle);
            it ++;
        }
        else
        {
            g_LogRecorder.WriteInfoLogEx(__FUNCTION__, "***Warning: Get Image[%s] size = 0.", it->second->pZP);
            delete it->second;
            it = m_mapZDRYInfo.erase(it);
        }

        nAdd++;
        if(nAdd % 2000 == 0)
        {
            g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "Aleady Download Image: %d.", nAdd);
        }
    }
    g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "Download Add Image Finished, Count[%d].", nAdd);
    return true;
}
//����������Ϣ����������
bool CUpdateLib::AddBatchInfo()
{
    g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "���Ӳ�������ͼƬ��Ϣ���������ط���, ����[%d].", m_mapZDRYInfo.size());
    char pHttpURL[128] = { 0 };
    sprintf_s(pHttpURL, sizeof(pHttpURL), "http://%s:%d/Store/addpicture", m_pDBInfo->pBatchStoreServerIP, m_pDBInfo->nBatchStoreServerPort);
    CHttpSyncClientPtr pClient(NULL);

    rapidjson::Document document;
    document.SetObject();
    rapidjson::Document::AllocatorType&allocator = document.GetAllocator();

    string sRespon = "";
    char * pImageInfo = new char[MAXIMAGESIZE];
    char * pImageBase64[10];
    for (int i = 0; i < 10; i++)
    {
        pImageBase64[i] = new char[MAXIMAGESIZE];
    }
    DWORD * pRealRead = new DWORD;
    int nFileSize = 0;
    int nLayout = 0;
    m_nLayoutFailed = 0;
    m_nLayoutSuccess = 0;
    MAPZDRYINFO::iterator it = m_mapZDRYInfo.begin();

    while (it != m_mapZDRYInfo.end())
    {
        rapidjson::Value array(rapidjson::kArrayType);
        int i = 0;
        while (it != m_mapZDRYInfo.end())
        {
            if (i < 10)
            {
                //��ȡͼƬ����������
                HANDLE hFileHandle = CreateFile(it->second->pImagePath,
                    GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL);
                if (INVALID_HANDLE_VALUE != hFileHandle)
                {
                    nFileSize = GetFileSize(hFileHandle, NULL);
                    if (nFileSize < MAXIMAGESIZE && nFileSize > 0)
                    {
                        ReadFile(hFileHandle, pImageInfo, nFileSize, pRealRead, NULL);

                        //������������Base64����, ���㴫��
                        string sFeature = ZBase64::Encode((BYTE*)pImageInfo, nFileSize);
                        strcpy(pImageBase64[i], sFeature.c_str());
                        pImageBase64[i][sFeature.size()] = '\0';
                        nLayout ++;

                        //����Json������
                        rapidjson::Value object(rapidjson::kObjectType);
                        object.AddMember("Face", rapidjson::StringRef(pImageBase64[i]), allocator);
                        object.AddMember("Name", rapidjson::StringRef(it->first.c_str()), allocator);
                        array.PushBack(object, allocator);

                        //g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "Add Image[%s].", it->second->pImagePath);
                    }
                    else
                    {
                        g_LogRecorder.WriteWarnLogEx(__FUNCTION__, "***Warning: ͼƬ���ݹ����Ϊ0[%s].", it->second->pImagePath);
                        m_nLayoutFailed ++;
                    }
                    CloseHandle(hFileHandle);
                }
                else
                {
                    g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "open file[%s] failed!", it->second->pImagePath);
                    m_nLayoutFailed ++;
                }
                
                i++;
                it++;
                if (i < 10 && it != m_mapZDRYInfo.end())
                {
                    continue;
                }
            }

            document.AddMember(JSONSTORELIBID, m_nCurrentUpdateLibID, allocator);
            document.AddMember(JSONLIBTYPE, 3, allocator);
            document.AddMember(JSONSTOREPHOTO, array, allocator);

            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            document.Accept(writer);
            string sDelInfo = string(buffer.GetString());

            //���������ط�������Ϣ
            pClient->OpenUrl("POST", pHttpURL, NULL, 0, (const BYTE*)sDelInfo.c_str(), sDelInfo.size());
            //g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "���������ط��������Ӳ���������Ϣ...");

            //��ȡ��Ӧ��Ϣ
            LPCBYTE pRespon = NULL;
            int nRepSize = 0;
            pClient->GetResponseBody(&pRespon, &nRepSize);
            sRespon.assign((char*)pRespon, nRepSize);
            //g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "�������ط����Ӧ���Ӳ���������Ϣ...");
            //�����Ӧ��Ϣ
            InsertAddInfoToDB((char*)sRespon.c_str());

            if(nLayout % 1000 == 0)
            {
                g_LogRecorder.WriteDebugLogEx(__FUNCTION__, 
                    "Total Count[%d], Handle[%d], Success[%d], Failed[%d].", 
                    m_mapZDRYInfo.size(), nLayout, m_nLayoutSuccess, m_nLayoutFailed);
            }
            i = 0;
            document.RemoveAllMembers();
            break;
        }
    }

    delete[]pImageInfo;
    for (int i = 0; i < 10; i++)
    {
        delete[]pImageBase64[i];
    }
    g_LogRecorder.WriteDebugLogEx(__FUNCTION__, 
        "��������ͼƬ������, Total Count[%d], Handle[%d], Success[%d], Failed[%d].",
        m_mapZDRYInfo.size(), nLayout, m_nLayoutSuccess, m_nLayoutFailed);
    return true;
}
bool CUpdateLib::InsertAddInfoToDB(char * pMsg)
{
    rapidjson::Document document;
    document.Parse(pMsg);
    if (document.HasParseError())
    {
        g_LogRecorder.WriteWarnLogEx(__FUNCTION__, "����Json��ʧ��[%s]", pMsg);
        return false;
    }

    if (document.HasMember("ErrorMessage") && document["ErrorMessage"].IsString())
    {
        string sErrorMsg = document["ErrorMessage"].GetString();
        g_LogRecorder.WriteInfoLogEx(__FUNCTION__, "***Warning: ���������񷵻���������������Ϣ[%s]", sErrorMsg.c_str());
        m_nLayoutFailed += 10;
        return false;
    }
    else
    {
        if (document.HasMember("Photo") && document["Photo"].IsArray() && document["Photo"].Size() > 0)
        {
            char pSQL[8192] = { 0 };
            int nRet = 0;

            //��ȡ���ص�ǰʱ��
            SYSTEMTIME sysTime;
            GetLocalTime(&sysTime);
            char pTime[32] = { 0 };
            sprintf_s(pTime, sizeof(pTime), "%04d-%02d-%02d %02d:%02d:%02d",
                sysTime.wYear, sysTime.wMonth, sysTime.wDay,
                sysTime.wHour, sysTime.wMinute, sysTime.wSecond);

            for (int i = 0; i < document["Photo"].Size(); i++)
            {
                if (document["Photo"][i].HasMember("Name")          && document["Photo"][i]["Name"].IsString()     &&
                    document["Photo"][i].HasMember("FaceUUID")      && document["Photo"][i]["FaceUUID"].IsString() &&
                    document["Photo"][i].HasMember("SavePath")      && document["Photo"][i]["SavePath"].IsString() &&
                    document["Photo"][i].HasMember("face_url")      && document["Photo"][i]["face_url"].IsString() &&
                    document["Photo"][i].HasMember("errormessage")  && document["Photo"][i]["errormessage"].IsString())
                {
                    string sName =      document["Photo"][i]["Name"].GetString();
                    string sFaceUUID =  document["Photo"][i]["FaceUUID"].GetString();
                    string sSavePath =  document["Photo"][i]["SavePath"].GetString();
                    string sFaceURL =   document["Photo"][i]["face_url"].GetString();
                    string sErrorMsg =  document["Photo"][i]["errormessage"].GetString();
                    if (sFaceUUID != "")
                    {
                        MAPZDRYINFO::iterator it = m_mapZDRYInfo.find(sName);
                        if (it != m_mapZDRYInfo.end())
                        {
                            sprintf_s(pSQL, sizeof(pSQL),
                                "INSERT into %s(faceuuid, imageid, time, localpath, feature, username, sexradio, idcard, "
                                "address, layoutlibid, controlstatus, updatetime, imageip, face_url, zdrybh, crimetype, crimeaddress, comment) "
                                "VALUES ('%s', '', '%s', '%s', '', '%s', '%s', '%s', "
                                "'%s', %d, 0, '%s', '%s', '%s', '%s', '%s', '%s', '%s')",
                                MYSQLSTOREFACEINFO, sFaceUUID.c_str(), pTime, sSavePath.c_str(), it->second->pName, it->second->pSex, it->second->pSFZH,
                                it->second->pAddress, m_nCurrentUpdateLibID, pTime, m_pDBInfo->pBatchStoreServerIP, sFaceURL.c_str(), it->first.c_str(), 
                                it->second->pAB, it->second->pCrimeAddress, it->second->pComment);
                            nRet = m_mysqltool.mysql_exec(_MYSQL_INSERT, pSQL);
                            if (nRet < 0)
                            {
                                g_LogRecorder.WriteWarnLogEx(__FUNCTION__, "***Warning: ���سɹ�������Ϣ���뵽���ݿ�ʧ��.\n%s", pSQL);
                                m_nLayoutFailed ++;
                            }
                            else
                            {
                                m_nLayoutSuccess ++;
                            }
                        }
                    }
                    else
                    {
                        g_LogRecorder.WriteInfoLogEx(__FUNCTION__, "[%s]���ʧ��, ErrorMsg: %s.", sName.c_str(), sErrorMsg.c_str());
                        m_nLayoutFailed ++;
                    }
                }
            }
        }

    }

    return true;
}
//ɾ�����಼����Ϣ���������ط���
bool CUpdateLib::DelBatchInfo()
{
    g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "ɾ�����಼����Ϣ, Count[%d].", m_mapLayoutBH.size());
    char pHttpURL[128] = { 0 };
    sprintf_s(pHttpURL, sizeof(pHttpURL), "http://%s:%d/Store/delpicture", m_pDBInfo->pBatchStoreServerIP, m_pDBInfo->nBatchStoreServerPort);
    CHttpSyncClientPtr pClient(NULL);

    rapidjson::Document document;
    document.SetObject();
    rapidjson::Document::AllocatorType&allocator = document.GetAllocator();

    map<string, string>::iterator it = m_mapLayoutBH.begin();
    while (it != m_mapLayoutBH.end())
    {
        rapidjson::Value array(rapidjson::kArrayType);
        int i = 0;
        while (it != m_mapLayoutBH.end())
        {
            if (i < 10)
            {
                rapidjson::Value object(rapidjson::kObjectType);
                object.AddMember(JSONFACEUUID, rapidjson::StringRef(it->second.c_str()), allocator);
                array.PushBack(object, allocator);
                i++;
                it++;
                if (i < 10 && it != m_mapLayoutBH.end())
                {
                    continue;
                }
            }

            if (i == 10 || it == m_mapLayoutBH.end())
            {
                document.AddMember(JSONSTORELIBID, m_nCurrentUpdateLibID, allocator);
                document.AddMember(JSONSTOREFACE, array, allocator);

                rapidjson::StringBuffer buffer;
                rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
                document.Accept(writer);
                string sDelInfo = string(buffer.GetString());

                //���������ط�������Ϣ
                pClient->OpenUrl("POST", pHttpURL, NULL, 0, (const BYTE*)sDelInfo.c_str(), sDelInfo.size());
                //g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "--���������ط�����ɾ����Ϣ...");

                //��ȡ��Ӧ��Ϣ
                LPCBYTE pImageBuf = NULL;
                int nImageLen = 0;
                pClient->GetResponseBody(&pImageBuf, &nImageLen);
                //g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "�������ط����Ӧɾ����Ϣ...");

                i = 0;
                document.RemoveAllMembers();
                break;
            }
        }
    }
    g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "���������ط�����ɾ����Ϣ����!");

    //�����ݿ�ɾ�����಼������
    char pSql[8096] = { 0 };
    int nRet = 0;
    map<string, string>::iterator itDel = m_mapLayoutBH.begin();
    int nDelete = 0;    //ɾ�������ص���Ա����
    for (; itDel != m_mapLayoutBH.end(); itDel++)
    {
        nDelete++;
        g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "ɾ�����಼����Ϣ, BH[%s].", itDel->first.c_str());

        //ɾ��storefaceinfo������
        sprintf_s(pSql, sizeof(pSql), "delete from %s where zdrybh = '%s'", MYSQLSTOREFACEINFO, itDel->first.c_str());
        nRet = m_mysqltool.mysql_exec(_MYSQL_DELETE, pSql);
        if (nRet < 0)
        {
            g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "Failed: %s", pSql);
            return false;
        }

        //ɾ��layoutresult������
        sprintf_s(pSql, sizeof(pSql), "delete from %s where layoutfaceuuid = '%s'", MYSQLLAYOUTRESULT, itDel->second.c_str());
        nRet = m_mysqltool.mysql_exec(_MYSQL_DELETE, pSql);
        if (nRet < 0)
        {
            g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "Failed: %s", pSql);
            return false;
        }
    }
    g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "ɾ�����಼����������, ɾ������: %d.", nDelete);
    return true;
}
//���¿�����������storecount��
bool CUpdateLib::UpdateStoreCount()
{
    char pSql[1024 * 8] = { 0 };

    sprintf_s(pSql, sizeof(pSql),
        "update %s set count = "
        "(select count(*) from %s where layoutlibid = %d) "
        "where storelibid = %d", 
        MYSQLSTORECOUNT, MYSQLSTOREFACEINFO, m_nCurrentUpdateLibID, m_nCurrentUpdateLibID);
    int nRet = m_mysqltool.mysql_exec(_MYSQL_UPDATA, pSql);
    if (nRet < 0)
    {
        g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "****Failed: %s", pSql);
        return false;
    }
    return true;
}