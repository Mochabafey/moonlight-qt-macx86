#include "backend/computermanager.h"
#include "streaming/session.h"

#include <QAbstractListModel>
#include <QVariantList>

class ComputerModel : public QAbstractListModel
{
    Q_OBJECT

    enum Roles
    {
        NameRole = Qt::UserRole,
        OnlineRole,
        PairedRole,
        BusyRole,
        WakeableRole,
        StatusUnknownRole,
        ServerSupportedRole,
        DetailsRole
    };

public:
    explicit ComputerModel(QObject* object = nullptr);

    // Must be called before any QAbstractListModel functions
    Q_INVOKABLE void initialize(ComputerManager* computerManager);

    QVariant data(const QModelIndex &index, int role) const override;

    int rowCount(const QModelIndex &parent) const override;

    virtual QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void deleteComputer(int computerIndex);

    Q_INVOKABLE QString generatePinString();

    Q_INVOKABLE void pairComputer(int computerIndex, QString pin);

    Q_INVOKABLE void testConnectionForComputer(int computerIndex);

    Q_INVOKABLE void wakeComputer(int computerIndex);

    // 不等轮询、也不管界面是不是显示离线，立刻对这台主机的候选地址做一次探测。
    // 用于「明明在线却被判离线」时强行把连接推起来。
    Q_INVOKABLE void forceConnectComputer(int computerIndex);

    // 同上，但探测成功后如果还没配对就立刻带 PIN 发起配对。
    Q_INVOKABLE void forcePairComputer(int computerIndex);

    Q_INVOKABLE void renameComputer(int computerIndex, QString name);

    Q_INVOKABLE Session* createSessionForCurrentGame(int computerIndex);

    Q_INVOKABLE QVariantList getConnectionAddressesForComputer(int computerIndex) const;

    Q_INVOKABLE bool hasMultipleConnectionAddresses(int computerIndex) const;

    Q_INVOKABLE bool setActiveAddressForComputer(int computerIndex, QString address, int port);

    // Undo a setActiveAddressForComputer() pin and go back to automatic selection.
    Q_INVOKABLE bool resetToAutomaticAddressForComputer(int computerIndex);

    // 「强制指定」：打开后只走固定地址，失败也不回退到其它候选链路。
    // 需要先 setActiveAddressForComputer() 选好一个具体地址才行。
    Q_INVOKABLE bool setAddressLockedForComputer(int computerIndex, bool locked);

    Q_INVOKABLE bool isAddressLockedForComputer(int computerIndex) const;

signals:
    void pairingCompleted(QVariant error);
    void connectionTestCompleted(int result, QString blockedPorts);

    // 强制探测的结果。success=false 表示所有候选地址都没能应答。
    void hostProbeCompleted(bool success, QString computerName);

    // 强制配对已经带着这个 PIN 发出去了，界面该把配对码弹出来。
    void forcePairStarted(QString pin, QString computerName);

private slots:
    void handleComputerStateChanged(NvComputer* computer);

    void handlePairingCompleted(NvComputer* computer, QString error);

    void handleHostProbeCompleted(NvComputer* computer, bool success);

private:
    QVector<NvComputer*> m_Computers;
    ComputerManager* m_ComputerManager;
    // 用户点了「强制配对」的那台主机。探测回来后据此决定要不要接着配对。
    NvComputer* m_ForcePairPending = nullptr;
};
