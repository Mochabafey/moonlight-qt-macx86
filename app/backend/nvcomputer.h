#pragma once

#include "nvhttp.h"
#include "nvaddress.h"

#include <QThread>
#include <QReadWriteLock>
#include <QSettings>
#include <QRunnable>
#include <QHash>

class CopySafeReadWriteLock : public QReadWriteLock
{
public:
    CopySafeReadWriteLock() = default;

    // Don't actually copy the QReadWriteLock
    CopySafeReadWriteLock(const CopySafeReadWriteLock&) : QReadWriteLock() {}
    CopySafeReadWriteLock& operator=(const CopySafeReadWriteLock &) { return *this; }
};

class NvComputer
{
    friend class PcMonitorThread;
    friend class ComputerManager;
    friend class PendingQuitTask;

private:
    void sortAppList();

    bool updateAppList(QVector<NvApp> newAppList);

    bool pendingQuit;

public:
    NvComputer() = default;

    // Caller is responsible for synchronizing read access to the other host
    NvComputer(const NvComputer&) = default;

    // Caller is responsible for synchronizing read access to the other host
    NvComputer& operator=(const NvComputer &) = default;

    explicit NvComputer(NvHTTP& http, QString serverInfo);

    explicit NvComputer(QSettings& settings);

    void
    setRemoteAddress(QHostAddress);

    bool
    update(const NvComputer& that);

    bool
    wake() const;

    enum ReachabilityType
    {
        RI_UNKNOWN,
        RI_LAN,
        RI_VPN,
    };

    ReachabilityType
    getActiveAddressReachability() const;

    QVector<NvAddress>
    uniqueAddresses() const;

    void
    pinAddress(const NvAddress& address);

    bool
    resetToAutomaticAddress();

    // 用户是否手动指定了一个连接地址（pinnedAddress 非空）。加锁读，别直接
    // 摸字段 —— 轮询线程随时可能改它。
    bool
    hasPinnedAddress() const;

    // 是否把连接严格锁死在 pinnedAddress 上。勾了「强制指定」之后轮询只试这一个
    // 地址，失败了也不回退到别的候选 —— 有些固定链路（专用网线、异地组网网关）
    // 必须走死某一条，自动回退反而会跑到别的网段上去。
    bool
    isAddressLocked() const;

    void
    setAddressLocked(bool locked);

    // 轮询实际使用的候选地址：勾了「强制指定」且已有固定地址时只返回那一个，
    // 否则返回按质量排好序的全部候选（用户指定的第一，验证过能用的按实测延迟
    // 从快到慢排，没验证过的垫底）。
    QVector<NvAddress>
    addressesToTry() const;

    // 地址质量账本：成功探测一次就记下往返毫秒数，供下一轮排序用。
    void
    markAddressLatency(const NvAddress& address, int milliseconds);

    // 返回最近一次成功探测的往返毫秒数，-1 表示这个地址还没测到过。
    int
    getAddressLatency(const NvAddress& address) const;

    void
    markAddressTestSucceeded(const NvAddress& address);

    bool
    hasAddressTestSucceeded(const NvAddress& address) const;

    void
    serialize(QSettings& settings, bool serializeApps) const;

    // Caller is responsible for synchronizing read access to both hosts
    bool
    isEqualSerialized(const NvComputer& that) const;

    enum PairState
    {
        PS_UNKNOWN,
        PS_PAIRED,
        PS_NOT_PAIRED
    };

    enum ComputerState
    {
        CS_UNKNOWN,
        CS_ONLINE,
        CS_OFFLINE
    };

    // Ephemeral traits
    ComputerState state;
    PairState pairState;
    NvAddress activeAddress;
    // 用户手动固定的连接地址。空表示自动选择。和 activeAddress 一样只活在
    // 会话内（不序列化）：activeAddress 本来就是重启后靠轮询重建的。
    // uniqueAddresses() 把它排在最前，所以轮询每次都先试它 —— 固定地址
    // 短暂掉线回退到别的地址后，恢复可达时会自动固定回来。
    NvAddress pinnedAddress;
    // 「强制指定」勾选态：只走 pinnedAddress，不回退。同样是会话内的临时状态。
    bool addressLocked = false;
    uint16_t activeHttpsPort;
    int currentGameId;
    QString gfeVersion;
    QString appVersion;
    QVector<NvDisplayMode> displayModes;
    int maxLumaPixelsHEVC;
    int serverCodecModeSupport;
    QString gpuModel;
    bool isSupportedServerVersion;

    // Persisted traits
    NvAddress localAddress;
    NvAddress remoteAddress;
    NvAddress ipv6Address;
    NvAddress manualAddress;
    QByteArray macAddress;
    QString name;
    bool hasCustomName;
    QString uuid;
    QSslCertificate serverCert;
    QVector<NvApp> appList;
    bool isNvidiaServerSoftware;
    // Remember to update isEqualSerialized() when adding fields here!

    // Synchronization
    mutable CopySafeReadWriteLock lock;

    // Static methods for pairname management
    static void savePairname(const QString& uuid, const QString& pairname);
    static QString getPairname(const QString& uuid);

private:
    // 下面两个都要在已经持有 lock 的情况下调用
    bool isAddressTestedLocked(const NvAddress& address) const;
    qint64 addressSortKeyLocked(const NvAddress& address) const;

    uint16_t externalPort;
    QVector<NvAddress> m_TestedAddresses;
    // 地址字符串 -> 最近一次成功探测的往返毫秒数。只活在会话内，用来给候选
    // 地址排序，不参与序列化（重启后自然会重新测一遍）。
    QHash<QString, int> m_AddressLatencies;
    mutable bool m_HasActiveAddressReachability = false;
    mutable NvAddress m_ActiveAddressReachabilityAddress;
    mutable ReachabilityType m_ActiveAddressReachability = RI_UNKNOWN;
};
