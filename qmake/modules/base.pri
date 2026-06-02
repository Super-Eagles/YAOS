YAOS_BASE_SOURCES += \
    src/config/Config.cpp \
    src/platform/network/FastNetHttpTransport.cpp \
    src/platform/network/FastNetWebSocketTransport.cpp \
    src/config/ConfigLoader.cpp \
    src/daemon/LocalDaemonProtocol.cpp \
    src/daemon/LocalDaemonServer.cpp \
    src/distributed/ContractsJson.cpp \
    src/distributed/LocalNodeRegistry.cpp \
    src/distributed/LocalTaskBus.cpp \
    src/distributed/RemoteControlClient.cpp \
    src/distributed/RemoteNodeRegistryClient.cpp \
    src/distributed/RemoteTaskBus.cpp \
    src/distributed/P2PCluster.cpp \
    src/bus/MessageBus.cpp \
    src/session/SessionManager.cpp

YAOS_BASE_HEADERS += \
    src/config/Config.h \
    src/platform/network/FastNetHttpTransport.h \
    src/platform/network/FastNetWebSocketTransport.h \
    src/config/ConfigLoader.h \
    src/daemon/LocalDaemonProtocol.h \
    src/daemon/LocalDaemonServer.h \
    src/distributed/ContractsJson.h \
    src/distributed/LocalNodeRegistry.h \
    src/distributed/LocalTaskBus.h \
    src/distributed/RemoteControlClient.h \
    src/distributed/RemoteNodeRegistryClient.h \
    src/distributed/RemoteTaskBus.h \
    src/distributed/P2PCluster.h \
    src/distributed/Contracts.h \
    src/bus/Message.h \
    src/bus/MessageBus.h \
    src/session/SessionManager.h
