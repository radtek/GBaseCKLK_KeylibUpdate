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

    //读取配置文件
    if (!m_pConfigRead->ReadConfig())
    {
        g_LogRecorder.WriteErrorLogEx(__FUNCTION__, "****Error: 读取配置文件参数错误!");
        return false;
    }
    else
    {
        g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "读取配置文件成功.");
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

        //连接MySQL数据库
        if (!m_mysqltool.mysql_connectDB(m_pDBInfo->pMysqlDBIP, m_pDBInfo->nMysqlDBPort, m_pDBInfo->pMysqlDBName,
            m_pDBInfo->pMysqlDBUser, m_pDBInfo->pMysqlDBPassword, "gb2312"))
        {
            g_LogRecorder.WriteErrorLogEx(__FUNCTION__, "****Error: 连接Mysql数据库失败[%s:%d:%s]!",
                m_pDBInfo->pMysqlDBIP, m_pDBInfo->nMysqlDBPort, m_pDBInfo->pMysqlDBName);
            return false;
        }
        else
        {
            g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "连接Mysql数据库成功[%s:%d:%s].",
                m_pDBInfo->pMysqlDBIP, m_pDBInfo->nMysqlDBPort, m_pDBInfo->pMysqlDBName);
        }

        //连接MySQL同步数据库
        if (!m_mysqlUpdateDB.mysql_connectDB(m_pDBInfo->pMysqlDBIP, m_pDBInfo->nMysqlDBPort, m_pDBInfo->pUpdateDBName,
            m_pDBInfo->pMysqlDBUser, m_pDBInfo->pMysqlDBPassword, "gb2312"))
        {
            g_LogRecorder.WriteErrorLogEx(__FUNCTION__, "****Error: 连接Mysql数据库失败[%s:%d:%s]!",
                m_pDBInfo->pMysqlDBIP, m_pDBInfo->nMysqlDBPort, m_pDBInfo->pUpdateDBName);
            return false;
        }
        else
        {
            g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "连接Mysql数据库成功[%s:%d:%s].",
                m_pDBInfo->pMysqlDBIP, m_pDBInfo->nMysqlDBPort, m_pDBInfo->pUpdateDBName);
        }

        map<int, string>::iterator it = m_mapUpdateLibInfo.begin();
        for (; it != m_mapUpdateLibInfo.end(); it++)
        {
            m_nCurrentUpdateLibID = it->first;
            //从同步数据库取黑名单库信息更新到Mysql主数据库
            if (!SyncKeyLibInfo())
            {
                g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "Error : 同步黑名单库信息失败!");
                return false;
            }
            //下载新增图片到本地保存
            if (!DownloadAddImage())
            {
                g_LogRecorder.WriteDebugLog(__FUNCTION__, "Error: 下载新增图片保存到本地失败!");
                return false;
            }
            //将新增图片信息推送到批量布控服务
            if (!AddBatchInfo())
            {
                return false;
            }
            //删除冗余布控信息到批量布控服务
            if (!DelBatchInfo())
            {
                return false;
            }
            //更新StoreCount表入库图片数量
            if (!UpdateStoreCount())
            {
                return false;
            }
            //清理信息, 为下一次更新准备
            if (!ClearInfo())
            {
                return false;
            }
        }

        m_setIgnore.clear();
        m_setga_ztryxx.clear();
        m_mysqltool.mysql_disconnectDB();
        g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "更新结束, 等待下一次更新.....");
    } while (WAIT_TIMEOUT == WaitForSingleObject(m_hStopEvent, 1000 * 60 * 30));
    
    return true;
}
bool CUpdateLib::StopUpdate()
{
    ClearInfo();
    SetEvent(m_hStopEvent);
    return true;
}
//清理所有信息, 为下一次更新准备
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
            g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "删除文件夹[%s]成功!", m_sCurrentImagePath.c_str());
        }
        else
        {
            g_LogRecorder.WriteWarnLogEx(__FUNCTION__, "***Warning: 删除文件夹[%s]失败!", m_sCurrentImagePath.c_str());
        }
    }
    m_sCurrentImagePath = "";
    return true;
}
//同步GBase黑名单库信息到Mysql
bool CUpdateLib::SyncKeyLibInfo()
{
    g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "\n***************开始同步黑名单库[%d][%s]信息***************", 
        m_nCurrentUpdateLibID, m_mapUpdateLibInfo[m_nCurrentUpdateLibID].c_str());
    int nRet = 0;
    int nNumber = 0;
    char pSql[1024 * 8] = { 0 };
    char pZDRYBH[64] = { 0 };
    char pFaceUUID[64] = { 0 };

    //获取ga_ztryxx表编号信息，用于插入时判断是否需要插入
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
        printf("%s表数据己获取, Count[%d].", MYSQLUPDATETABLE, m_setga_ztryxx.size());
    }

    //获取ga_ignore表编号信息，用于插入时判断是否需要插入
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
        printf("%s表数据己获取, Count[%d].\n", GAIGNORETABLE, m_setIgnore.size());
    }
    

    //获取mysql storefaceinfo里所有重点人员信息, 用于后面的更新或插入;
    g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "获取Mysql数据库黑名单库人员信息...");
    sprintf_s(pSql, sizeof(pSql),
        "Select zdrybh, faceuuid from %s where LayoutLibId = %d and zdrybh is not null", MYSQLSTOREFACEINFO, m_nCurrentUpdateLibID);
    nRet = m_mysqltool.mysql_exec(_MYSQL_SELECT, pSql);
    if (nRet > 0)
    {
        while (m_mysqltool.mysql_getNextRow())
        {
            strcpy_s(pZDRYBH, sizeof(pZDRYBH), m_mysqltool.mysql_getRowStringValue("zdrybh"));
            strcpy_s(pFaceUUID, sizeof(pFaceUUID), m_mysqltool.mysql_getRowStringValue("faceuuid"));
            //插入所有正在布控的人员编号信息, 后面判断是否需要删除冗余的数据
            m_mapLayoutBH.insert(make_pair(pZDRYBH, pFaceUUID));
        }
    }
    g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "获取Mysql数据库黑名单库[%d]人员编号信息结束, 人员总数[%d].", m_nCurrentUpdateLibID, m_mapLayoutBH.size());

    //生成新增图片文件加地址
    SYSTEMTIME sysTime;
    GetLocalTime(&sysTime);
    char pTimeID[20] = { 0 };
    sprintf_s(pTimeID, sizeof(pTimeID), "%d-%04d%02d%02d%02d%02d%02d/",
        m_nCurrentUpdateLibID, sysTime.wYear, sysTime.wMonth, sysTime.wDay, sysTime.wHour, sysTime.wMinute, sysTime.wSecond);
    string sKeylibPath = m_pDBInfo->pSavePath;
    m_sCurrentImagePath = m_pDBInfo->pSavePath + string(pTimeID);
    CreateDirectory(sKeylibPath.c_str(), NULL);
    CreateDirectory(m_sCurrentImagePath.c_str(), NULL);
    g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "创建文件夹[%s]成功.", m_sCurrentImagePath.c_str());


    //获取同步数据库表黑名单库人员信息, 没有人员编号的, 以身份证号为人员编号
    switch (m_nCurrentUpdateLibID)
    {
        case ID_QGWF_PQ: case ID_QGWF_RSDQ: //全国违法_扒窃, 全国违法_入室盗窃
        {
            sprintf_s(pSql, sizeof(pSql),
                "select XM, XB, SFZH, JZD, MZ, CSRQ, SG, HJD, ODS_INPUT_TIME, ZP "
                "from %s where SFZH is not NULL and ZP is not NULL group by SFZH", 
                m_mapUpdateLibInfo[m_nCurrentUpdateLibID].c_str());
            break;
        } 
        case ID_QGWF_PQLCYS: case ID_QGWF_RSDQLCYS: //全国违法_两次扒窃以上, 全国违法_入室盗窃两次以上
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
        g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "执行SQL语句失败!");
        return false;
    }
    else
    {
        g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "执行SQL语句, 获取%s黑名单库人员信息中...!", m_mapUpdateLibInfo[m_nCurrentUpdateLibID].c_str());
    }
    
    char RYBH[256] = { 0 };     //人员编号
    char XM[256] = { 0 };       //姓名
    char XB[256] = { 0 };       //性别
    char ZJHM[256] = { 0 };     //证件号码
    char JZD[256] = { 0 };      //居住地
    char AB[256] = { 0 };       //案别
    char ABXL[256] = { 0 };     //案别细类
    char MZ[256] = { 0 };       //民族
    char CSRQ[256] = { 0 };     //出生日期  
    char SG[256] = { 0 };       //身高
    char HJDQH[256] = { 0 };    //户籍地区划
    char HJDXZ[256] = { 0 };    //户籍地详细住址
    char YWGXBM[256] = { 0 };   //业务管辖部门
    char LRDW[2014] = { 0 };    //录入单位
    char LRSJ[256] = { 0 };     //录入时间
    char WFJL[1024 * 8] = { 0 };//违法记录
    char XZCS[1024] = { 0 };    //选择处所
    char BM[256] = { 0 };       //别名(绰号)
    char ZHDW[256] = { 0 };     //抓获单位
    char ZP[1024] = { 0 };      //照片(URL)

    int nAdd = 0;
    char pComment[1024 * 8] = { 0 };    //备注
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
                if (ID_QGWF_PQ == m_nCurrentUpdateLibID)    strcpy_s(AB, sizeof(AB), "全国违法扒窃");
                else strcpy_s(AB, sizeof(AB),   "全国违法入室盗窃");
                strcpy_s(MZ,    sizeof(MZ),     m_mysqlUpdateDB.mysql_getRowStringValue("MZ"));
                strcpy_s(CSRQ,  sizeof(CSRQ),   m_mysqlUpdateDB.mysql_getRowStringValue("CSRQ"));
                strcpy_s(SG,    sizeof(SG),     m_mysqlUpdateDB.mysql_getRowStringValue("SG"));
                strcpy_s(HJDQH, sizeof(HJDQH),  "null");
                strcpy_s(HJDXZ, sizeof(HJDXZ),  m_mysqlUpdateDB.mysql_getRowStringValue("HJD"));
                strcpy_s(LRSJ,  sizeof(LRSJ),   m_mysqlUpdateDB.mysql_getRowStringValue("ODS_INPUT_TIME"));
                strcpy_s(XZCS,  sizeof(XZCS),   "未知");
                strcpy_s(WFJL,  sizeof(WFJL),   "null");
                strcpy_s(ZP,    sizeof(ZP),     m_mysqlUpdateDB.mysql_getRowStringValue("ZP"));
                //test
                //strcpy_s(ZP, sizeof(ZP), "http://35.24.22.179/D/StoreSTLibServer/106/4637442370A94a8d920416C46591D1DA.jpg");
                sprintf_s(pComment, sizeof(pComment), "案别: %s; \n录入时间: %s; \n违法记录: %s", AB, LRSJ, WFJL);
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
                strcpy_s(XZCS,  sizeof(XZCS),   "未知");
                strcpy_s(WFJL,  sizeof(WFJL), m_mysqlUpdateDB.mysql_getRowStringValue("JYAQ"));
                strcpy_s(ZP,    sizeof(ZP),     m_mysqlUpdateDB.mysql_getRowStringValue("ZP"));
                sprintf_s(pComment, sizeof(pComment), "案别: %s; \n录入时间: %s; \n违法记录: %s", AB, LRSJ, WFJL);
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
                    "案别: %s; \n业务管辖部门: %s;\n录入单位: %s; \n录入时间: %s; \n违法记录: %s",
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
                    "案别: %s; \n案别细类: %s; \n业务管辖部门: %s;\n录入单位: %s; \n录入时间: %s; \n违法记录: %s",
                    AB, ABXL, YWGXBM, LRDW, LRSJ, WFJL);
                break;
            }
            case ID_ZTRYCX_PQ: case ID_ZTRYCX_RSDQ:
            {
                strcpy_s(RYBH,  sizeof(RYBH),   m_mysqlUpdateDB.mysql_getRowStringValue("RYBH"));
                strcpy_s(XM,    sizeof(XM),     m_mysqlUpdateDB.mysql_getRowStringValue("XM"));
                strcpy_s(XB,    sizeof(XB),     m_mysqlUpdateDB.mysql_getRowStringValue("XB"));
                strcpy_s(ZJHM,  sizeof(ZJHM),   m_mysqlUpdateDB.mysql_getRowStringValue("ZJHM"));
                strcpy_s(JZD,   sizeof(JZD),    "未知");
                strcpy_s(AB,    sizeof(AB),     m_mysqlUpdateDB.mysql_getRowStringValue("AB"));
                strcpy_s(MZ,    sizeof(MZ),     "未知");
                strcpy_s(CSRQ,  sizeof(CSRQ),   "未知");
                strcpy_s(SG,    sizeof(SG),     "未知");
                strcpy_s(HJDQH, sizeof(HJDQH),  m_mysqlUpdateDB.mysql_getRowStringValue("HJDQH"));
                strcpy_s(HJDXZ, sizeof(HJDXZ),  "未知");
                strcpy_s(LRDW, sizeof(LRDW),    m_mysqlUpdateDB.mysql_getRowStringValue("LADW"));
                strcpy_s(LRSJ, sizeof(LRSJ),    m_mysqlUpdateDB.mysql_getRowStringValue("RBDJKRQ"));
                strcpy_s(XZCS, sizeof(XZCS),    "未知");
                strcpy_s(ZHDW,  sizeof(ZHDW),   m_mysqlUpdateDB.mysql_getRowStringValue("ZHDW"));
                strcpy_s(WFJL,  sizeof(WFJL),   m_mysqlUpdateDB.mysql_getRowStringValue("JYAQ"));
                strcpy_s(ZP,    sizeof(ZP),     m_mysqlUpdateDB.mysql_getRowStringValue("ZP"));
                sprintf_s(pComment, sizeof(pComment),
                    "案别: %s; \n立案单位: %s; \n抓获单位: %s; \n违法记录: %s",
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
        //身份证编号己存在则更新, 未存在则插入Mysql同步表
        if (it != m_mapLayoutBH.end())
        {
            m_mapLayoutBH.erase(it);
        }
        else
        {
            set<string>::iterator itIgnore = m_setIgnore.find(RYBH);  //查找确定此编号是否需要忽略
            if (itIgnore != m_setIgnore.end())
            {
                printf("Ignore BH[%s].\n", RYBH);
            }
            else
            {
                nAdd++;
                LPZDRYINFO pZDRYInfo = new ZDRYINFO;
                strcpy(pZDRYInfo->pName, XM);
                if (string(XB) == "女")
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


                itJudeg = m_setga_ztryxx.find(RYBH);  //判断下ga_ztryxx表是否己存在这条数据, 不存在则插入
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
                        g_LogRecorder.WriteWarnLogEx(__FUNCTION__, "***Warning: 同步数据库同步重点人员信息到Mysql表失败.");
                    }
                }
            }
        }

        nNumber++;
        if (nNumber % 2000 == 0)
        {
            g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "己同步人员信息: %d", nNumber);
        }
    }

    g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "同步黑名单库人员信息表结束, 同步[%d], Add[%d].", nNumber, nAdd);
    return true;
}
//下载新增同步人脸图片到本地保存
bool CUpdateLib::DownloadAddImage()
{
    g_LogRecorder.WriteDebugLog(__FUNCTION__, "开始下载新增人脸图片...");
   
    DWORD dwWrite = 0;
    int nLen = 0;
    string sTempPath = "";

    CHttpSyncClientPtr pClient(NULL);
    char pSql[1024 * 8] = { 0 };
    int nAdd = 0;
    MAPZDRYINFO::iterator it = m_mapZDRYInfo.begin();
    while (it != m_mapZDRYInfo.end())
    {
        //取图片
        pClient->OpenUrl("GET", it->second->pZP);

        //收取回应消息
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
//新增布控信息到批量布控
bool CUpdateLib::AddBatchInfo()
{
    g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "增加布控人脸图片信息到批量布控服务, 总数[%d].", m_mapZDRYInfo.size());
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
                //读取图片二进制数据
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

                        //将二进制数据Base64编码, 方便传输
                        string sFeature = ZBase64::Encode((BYTE*)pImageInfo, nFileSize);
                        strcpy(pImageBase64[i], sFeature.c_str());
                        pImageBase64[i][sFeature.size()] = '\0';
                        nLayout ++;

                        //生成Json组数据
                        rapidjson::Value object(rapidjson::kObjectType);
                        object.AddMember("Face", rapidjson::StringRef(pImageBase64[i]), allocator);
                        object.AddMember("Name", rapidjson::StringRef(it->first.c_str()), allocator);
                        array.PushBack(object, allocator);

                        //g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "Add Image[%s].", it->second->pImagePath);
                    }
                    else
                    {
                        g_LogRecorder.WriteWarnLogEx(__FUNCTION__, "***Warning: 图片数据过大或为0[%s].", it->second->pImagePath);
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

            //向批量布控服务发送消息
            pClient->OpenUrl("POST", pHttpURL, NULL, 0, (const BYTE*)sDelInfo.c_str(), sDelInfo.size());
            //g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "向批量布控服务发送增加布控人脸信息...");

            //收取回应消息
            LPCBYTE pRespon = NULL;
            int nRepSize = 0;
            pClient->GetResponseBody(&pRespon, &nRepSize);
            sRespon.assign((char*)pRespon, nRepSize);
            //g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "批量布控服务回应增加布控人脸信息...");
            //处理回应信息
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
        "处理新增图片入库结束, Total Count[%d], Handle[%d], Success[%d], Failed[%d].",
        m_mapZDRYInfo.size(), nLayout, m_nLayoutSuccess, m_nLayoutFailed);
    return true;
}
bool CUpdateLib::InsertAddInfoToDB(char * pMsg)
{
    rapidjson::Document document;
    document.Parse(pMsg);
    if (document.HasParseError())
    {
        g_LogRecorder.WriteWarnLogEx(__FUNCTION__, "解析Json串失败[%s]", pMsg);
        return false;
    }

    if (document.HasMember("ErrorMessage") && document["ErrorMessage"].IsString())
    {
        string sErrorMsg = document["ErrorMessage"].GetString();
        g_LogRecorder.WriteInfoLogEx(__FUNCTION__, "***Warning: 批量入库服务返回增加人脸错误信息[%s]", sErrorMsg.c_str());
        m_nLayoutFailed += 10;
        return false;
    }
    else
    {
        if (document.HasMember("Photo") && document["Photo"].IsArray() && document["Photo"].Size() > 0)
        {
            char pSQL[8192] = { 0 };
            int nRet = 0;

            //获取本地当前时间
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
                                g_LogRecorder.WriteWarnLogEx(__FUNCTION__, "***Warning: 布控成功人脸信息插入到数据库失败.\n%s", pSQL);
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
                        g_LogRecorder.WriteInfoLogEx(__FUNCTION__, "[%s]入库失败, ErrorMsg: %s.", sName.c_str(), sErrorMsg.c_str());
                        m_nLayoutFailed ++;
                    }
                }
            }
        }

    }

    return true;
}
//删除冗余布控信息到批量布控服务
bool CUpdateLib::DelBatchInfo()
{
    g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "删除冗余布控信息, Count[%d].", m_mapLayoutBH.size());
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

                //向批量布控服务发送消息
                pClient->OpenUrl("POST", pHttpURL, NULL, 0, (const BYTE*)sDelInfo.c_str(), sDelInfo.size());
                //g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "--向批量布控服务发送删除信息...");

                //收取回应消息
                LPCBYTE pImageBuf = NULL;
                int nImageLen = 0;
                pClient->GetResponseBody(&pImageBuf, &nImageLen);
                //g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "批量布控服务回应删除信息...");

                i = 0;
                document.RemoveAllMembers();
                break;
            }
        }
    }
    g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "向批量布控服务发送删除信息结束!");

    //从数据库删除冗余布控数据
    char pSql[8096] = { 0 };
    int nRet = 0;
    map<string, string>::iterator itDel = m_mapLayoutBH.begin();
    int nDelete = 0;    //删除布控重点人员总数
    for (; itDel != m_mapLayoutBH.end(); itDel++)
    {
        nDelete++;
        g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "删除冗余布控信息, BH[%s].", itDel->first.c_str());

        //删除storefaceinfo表数据
        sprintf_s(pSql, sizeof(pSql), "delete from %s where zdrybh = '%s'", MYSQLSTOREFACEINFO, itDel->first.c_str());
        nRet = m_mysqltool.mysql_exec(_MYSQL_DELETE, pSql);
        if (nRet < 0)
        {
            g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "Failed: %s", pSql);
            return false;
        }

        //删除layoutresult表数据
        sprintf_s(pSql, sizeof(pSql), "delete from %s where layoutfaceuuid = '%s'", MYSQLLAYOUTRESULT, itDel->second.c_str());
        nRet = m_mysqltool.mysql_exec(_MYSQL_DELETE, pSql);
        if (nRet < 0)
        {
            g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "Failed: %s", pSql);
            return false;
        }
    }
    g_LogRecorder.WriteDebugLogEx(__FUNCTION__, "删除冗余布控人数结束, 删除数量: %d.", nDelete);
    return true;
}
//更新库人脸数量到storecount表
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