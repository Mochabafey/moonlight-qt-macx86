#include "computermodel.h"
#include "appmodel.h"
#include "backend/nvcomputer.h"

#include <QDebug>
#include <QReadLocker>
#include <QThreadPool>
#include <QWriteLocker>


ComputerModel::ComputerModel(QObject* object)
    : QAbstractListModel(object) {}

void ComputerModel::initialize(ComputerManager* computerManager)
{
    m_ComputerManager = computerManager;
    connect(m_ComputerManager, &ComputerManager::computerStateChanged,
            this, &ComputerModel::handleComputerStateChanged);
    connect(m_ComputerManager, &ComputerManager::pairingCompleted,
            this, &ComputerModel::handlePairingCompleted);
    connect(m_ComputerManager, &ComputerManager::hostProbeCompleted,
            this, &ComputerModel::handleHostProbeCompleted);

    m_Computers = m_ComputerManager->getComputers();
}

QVariant ComputerModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    Q_ASSERT(index.row() < m_Computers.count());

    NvComputer* computer = m_Computers[index.row()];
    QReadLocker lock(&computer->lock);

    switch (role) {
    case NameRole:
        return computer->name;
    case OnlineRole:
        return computer->state == NvComputer::CS_ONLINE;
    case PairedRole:
        return computer->pairState == NvComputer::PS_PAIRED;
    case BusyRole:
        return computer->currentGameId != 0;
    case WakeableRole:
        return !computer->macAddress.isEmpty();
    case StatusUnknownRole:
        return computer->state == NvComputer::CS_UNKNOWN;
    case ServerSupportedRole:
        return computer->isSupportedServerVersion;
    case DetailsRole: {
        QString state, pairState;

        switch (computer->state) {
        case NvComputer::CS_ONLINE:
            state = tr("Online");
            break;
        case NvComputer::CS_OFFLINE:
            state = tr("Offline");
            break;
        default:
            state = tr("Unknown");
            break;
        }

        switch (computer->pairState) {
        case NvComputer::PS_PAIRED:
            pairState = tr("Paired");
            break;
        case NvComputer::PS_NOT_PAIRED:
            pairState = tr("Unpaired");
            break;
        default:
            pairState = tr("Unknown");
            break;
        }

        QString pairname = NvComputer::getPairname(computer->uuid);
        QString pairnameInfo = pairname.isEmpty() ? tr("Unknown") : pairname;
        
        return tr("Name: %1").arg(computer->name) + '\n' +
               tr("Status: %1").arg(state) + '\n' +
               tr("Active Address: %1").arg(computer->activeAddress.toString()) + '\n' +
               tr("UUID: %1").arg(computer->uuid) + '\n' +
               tr("Pair Name: %1").arg(pairnameInfo) + '\n' +
               tr("Local Address: %1").arg(computer->localAddress.toString()) + '\n' +
               tr("Remote Address: %1").arg(computer->remoteAddress.toString()) + '\n' +
               tr("IPv6 Address: %1").arg(computer->ipv6Address.toString()) + '\n' +
               tr("Manual Address: %1").arg(computer->manualAddress.toString()) + '\n' +
               tr("MAC Address: %1").arg(computer->macAddress.isEmpty() ? tr("Unknown") : QString(computer->macAddress.toHex(':'))) + '\n' +
               tr("Pair State: %1").arg(pairState) + '\n' +
               tr("Running Game ID: %1").arg(computer->state == NvComputer::CS_ONLINE ? QString::number(computer->currentGameId) : tr("Unknown")) + '\n' +
               tr("HTTPS Port: %1").arg(computer->state == NvComputer::CS_ONLINE ? QString::number(computer->activeHttpsPort) : tr("Unknown"));
    }
    default:
        return QVariant();
    }
}

int ComputerModel::rowCount(const QModelIndex& parent) const
{
    // We should not return a count for valid index values,
    // only the parent (which will not have a "valid" index).
    if (parent.isValid()) {
        return 0;
    }

    return m_Computers.count();
}

QHash<int, QByteArray> ComputerModel::roleNames() const
{
    QHash<int, QByteArray> names;

    names[NameRole] = "name";
    names[OnlineRole] = "online";
    names[PairedRole] = "paired";
    names[BusyRole] = "busy";
    names[WakeableRole] = "wakeable";
    names[StatusUnknownRole] = "statusUnknown";
    names[ServerSupportedRole] = "serverSupported";
    names[DetailsRole] = "details";

    return names;
}

Session* ComputerModel::createSessionForCurrentGame(int computerIndex)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    NvComputer* computer = m_Computers[computerIndex];

    // We must currently be streaming a game to use this function
    Q_ASSERT(computer->currentGameId != 0);

    for (NvApp& app : computer->appList) {
        if (app.id == computer->currentGameId) {
            return new Session(computer, app);
        }
    }

    // We have a current running app but it's not in our app list
    Q_ASSERT(false);
    return nullptr;
}

QVariantList ComputerModel::getConnectionAddressesForComputer(int computerIndex) const
{
    QVariantList addresses;

    if (computerIndex < 0 || computerIndex >= m_Computers.count()) {
        qWarning() << "Invalid computer index for getConnectionAddressesForComputer:" << computerIndex;
        return addresses;
    }

    // 和 AppView 那边共用一份构造逻辑：两边喂的是同一个 QML 组件，
    // 条目形状和「哪一项算选中」的判定必须一致。
    return AppModel::buildConnectionAddressList(m_Computers[computerIndex]);
}

bool ComputerModel::hasMultipleConnectionAddresses(int computerIndex) const
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) {
        qWarning() << "Invalid computer index for hasMultipleConnectionAddresses:" << computerIndex;
        return false;
    }

    // 数的是全部已知地址，不只是验证过的 —— 用户应该总能从所有地址里挑。
    return m_Computers[computerIndex]->uniqueAddresses().count() > 1;
}

bool ComputerModel::setActiveAddressForComputer(int computerIndex, QString address, int port)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) {
        qWarning() << "Invalid computer index for setActiveAddressForComputer:" << computerIndex;
        return false;
    }

    if (address.isEmpty() || port <= 0) {
        qWarning() << "Invalid address for setActiveAddressForComputer:" << address << port;
        return false;
    }

    NvComputer* computer = m_Computers[computerIndex];
    NvAddress selectedAddress(address, static_cast<uint16_t>(port));

    // Verify the address is one of the known addresses
    bool found = false;
    for (const NvAddress& addr : computer->uniqueAddresses()) {
        if (addr == selectedAddress) {
            found = true;
            break;
        }
    }
    if (!found) {
        qWarning() << "Address is not a known address:" << selectedAddress.toString();
        return false;
    }

    computer->pinAddress(selectedAddress);

    emit dataChanged(createIndex(computerIndex, 0), createIndex(computerIndex, 0));
    return true;
}

bool ComputerModel::setAddressLockedForComputer(int computerIndex, bool locked)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) {
        qWarning() << "Invalid computer index for setAddressLockedForComputer:" << computerIndex;
        return false;
    }

    NvComputer* computer = m_Computers[computerIndex];

    // 没有固定地址就谈不上「强制指定」—— 勾了也只会把自动模式锁死在一个
    // 空地址上，所以这里直接拒绝，让界面把勾去掉。
    if (locked && !computer->hasPinnedAddress()) {
        qWarning() << "Refusing to lock the connection without a pinned address";
        return false;
    }

    computer->setAddressLocked(locked);

    emit dataChanged(createIndex(computerIndex, 0), createIndex(computerIndex, 0));
    return true;
}

bool ComputerModel::isAddressLockedForComputer(int computerIndex) const
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) {
        return false;
    }

    return m_Computers[computerIndex]->isAddressLocked();
}

bool ComputerModel::resetToAutomaticAddressForComputer(int computerIndex)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) {
        qWarning() << "Invalid computer index for resetToAutomaticAddressForComputer:" << computerIndex;
        return false;
    }

    if (!m_Computers[computerIndex]->resetToAutomaticAddress()) {
        return false;
    }

    emit dataChanged(createIndex(computerIndex, 0), createIndex(computerIndex, 0));
    return true;
}

void ComputerModel::deleteComputer(int computerIndex)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    beginRemoveRows(QModelIndex(), computerIndex, computerIndex);

    // m_Computer[computerIndex] will be deleted by this call
    m_ComputerManager->deleteHost(m_Computers[computerIndex]);

    // Remove the now invalid item
    m_Computers.removeAt(computerIndex);

    endRemoveRows();
}

class DeferredWakeHostTask : public QRunnable
{
public:
    DeferredWakeHostTask(NvComputer* computer)
        : m_Computer(computer) {}

    void run()
    {
        m_Computer->wake();
    }

private:
    NvComputer* m_Computer;
};

void ComputerModel::wakeComputer(int computerIndex)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    DeferredWakeHostTask* wakeTask = new DeferredWakeHostTask(m_Computers[computerIndex]);
    QThreadPool::globalInstance()->start(wakeTask);
}

void ComputerModel::renameComputer(int computerIndex, QString name)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    m_ComputerManager->renameHost(m_Computers[computerIndex], name);
}

QString ComputerModel::generatePinString()
{
    return m_ComputerManager->generatePinString();
}

class DeferredTestConnectionTask : public QObject, public QRunnable
{
    Q_OBJECT
public:
    void run()
    {
        unsigned int portTestResult = LiTestClientConnectivity("qt.conntest.moonlight-stream.org", 443, ML_PORT_FLAG_ALL);
        if (portTestResult == ML_TEST_RESULT_INCONCLUSIVE) {
            emit connectionTestCompleted(-1, QString());
        }
        else {
            char blockedPorts[512];
            LiStringifyPortFlags(portTestResult, "\n", blockedPorts, sizeof(blockedPorts));
            emit connectionTestCompleted(portTestResult, QString(blockedPorts));
        }
    }

signals:
    void connectionTestCompleted(int result, QString blockedPorts);
};

void ComputerModel::testConnectionForComputer(int)
{
    DeferredTestConnectionTask* testConnectionTask = new DeferredTestConnectionTask();
    QObject::connect(testConnectionTask, &DeferredTestConnectionTask::connectionTestCompleted,
                     this, &ComputerModel::connectionTestCompleted);
    QThreadPool::globalInstance()->start(testConnectionTask);
}

void ComputerModel::pairComputer(int computerIndex, QString pin)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    m_ComputerManager->pairHost(m_Computers[computerIndex], pin);
}

void ComputerModel::forceConnectComputer(int computerIndex)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) {
        qWarning() << "Invalid computer index for forceConnectComputer:" << computerIndex;
        emit hostProbeCompleted(false, QString());
        return;
    }

    m_ComputerManager->probeHostNow(m_Computers[computerIndex]);
}

void ComputerModel::forcePairComputer(int computerIndex)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) {
        qWarning() << "Invalid computer index for forcePairComputer:" << computerIndex;
        emit hostProbeCompleted(false, QString());
        return;
    }

    // 配对的前提是链路通。先把这次强制探测抛出去，等它回来再决定要不要发配对
    // 请求 —— 直接盲发配对只会拿到一个超时错误，反而看不出是网络问题还是
    // PairStatus 的问题。
    m_ForcePairPending = m_Computers[computerIndex];
    m_ComputerManager->probeHostNow(m_ForcePairPending);
}

void ComputerModel::handleHostProbeCompleted(NvComputer* computer, bool success)
{
    // 主机可能在探测期间被删掉了，这时候别再碰它。
    if (computer == nullptr || !m_Computers.contains(computer)) {
        if (computer == m_ForcePairPending) {
            m_ForcePairPending = nullptr;
        }
        emit hostProbeCompleted(false, QString());
        return;
    }

    const QString computerName = computer->name;

    if (computer == m_ForcePairPending) {
        m_ForcePairPending = nullptr;

        NvComputer::PairState pairState;
        {
            QReadLocker lock(&computer->lock);
            pairState = computer->pairState;
        }

        if (success && pairState != NvComputer::PS_PAIRED) {
            // 链路通了但还没配对：直接把配对码和请求一起发出去，界面负责把
            // PIN 显示出来。这就是「离线状态下也能强制配对」的落点。
            const QString pin = generatePinString();
            m_ComputerManager->pairHost(computer, pin);
            emit forcePairStarted(pin, computerName);
            return;
        }
    }

    emit hostProbeCompleted(success, computerName);
}

void ComputerModel::handlePairingCompleted(NvComputer*, QString error)
{
    emit pairingCompleted(error.isEmpty() ? QVariant() : error);
}

void ComputerModel::handleComputerStateChanged(NvComputer* computer)
{
    QVector<NvComputer*> newComputerList = m_ComputerManager->getComputers();

    // Reset the model if the structural layout of the list has changed
    if (m_Computers != newComputerList) {
        beginResetModel();
        m_Computers = newComputerList;
        endResetModel();
    }
    else {
        // Let the view know that this specific computer changed
        int index = m_Computers.indexOf(computer);
        emit dataChanged(createIndex(index, 0), createIndex(index, 0));
    }
}

#include "computermodel.moc"
