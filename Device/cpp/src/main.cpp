#include <aws/crt/Api.h>
#include <aws/crt/StlAllocator.h>
#include <aws/crt/auth/Credentials.h>
#include <aws/crt/io/TlsOptions.h>
#include <aws/iot/MqttClient.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <algorithm>
#include <condition_variable>
#include <iostream>
#include <mutex>

void test() {
    /************************ Setup the Lib ****************************/
    /*
     * Do the global initialization for the API.
     */
    Aws::Crt::ApiHandle apiHandle;

    const char *const endpoint = std::getenv("AWS_IOT_CORE_ENDPOINT");
    if (endpoint == nullptr) {
        std::cerr << "set AWS_IOT_CORE_ENDPOINT as an environment value"
                  << std::endl;
        exit(1);
    }
    Aws::Crt::String certificatePath =
        "./certificates/device_cert_filename.pem";
    Aws::Crt::String keyPath = "./certificates/device_cert_key_filename.key";
    Aws::Crt::String caFile = "./certificates/AmazonRootCA1.pem";
    Aws::Crt::String topic = "test/iot";
    Aws::Crt::String clientId = "Thing1";
    Aws::Crt::String signingRegion;
    Aws::Crt::String proxyHost;
    // uint16_t proxyPort(8080);

    Aws::Crt::String x509Endpoint;
    Aws::Crt::String x509ThingName;
    Aws::Crt::String x509RoleAlias;
    Aws::Crt::String x509CertificatePath;
    Aws::Crt::String x509KeyPath;
    Aws::Crt::String x509RootCAFile;

    // bool useWebSocket = false;
    // bool useX509 = false;

    /********************** Now Setup an Mqtt Client ******************/
    /*
     * You need an event loop group to process IO events.
     * If you only have a few connections, 1 thread is ideal
     */
    Aws::Crt::Io::EventLoopGroup eventLoopGroup(1);
    if (!eventLoopGroup) {
        fprintf(stderr, "Event Loop Group Creation failed with error %s\n",
                Aws::Crt::ErrorDebugString(eventLoopGroup.LastError()));
        exit(-1);
    }

    Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 1, 5);
    Aws::Crt::Io::ClientBootstrap bootstrap(eventLoopGroup,
                                            defaultHostResolver);

    if (!bootstrap) {
        fprintf(stderr, "ClientBootstrap failed with error %s\n",
                Aws::Crt::ErrorDebugString(bootstrap.LastError()));
        exit(-1);
    }

    Aws::Crt::Io::TlsContext x509TlsCtx;
    Aws::Iot::MqttClientConnectionConfigBuilder builder;

    if (!certificatePath.empty() && !keyPath.empty()) {
        builder = Aws::Iot::MqttClientConnectionConfigBuilder(
            certificatePath.c_str(), keyPath.c_str());
    }

    if (!caFile.empty()) {
        builder.WithCertificateAuthority(caFile.c_str());
    }

    builder.WithEndpoint(endpoint);

    auto clientConfig = builder.Build();

    if (!clientConfig) {
        fprintf(stderr,
                "Client Configuration initialization failed with error %s\n",
                Aws::Crt::ErrorDebugString(clientConfig.LastError()));
        exit(-1);
    }

    Aws::Iot::MqttClient mqttClient(bootstrap);
    /*
     * Since no exceptions are used, always check the bool operator
     * when an error could have occurred.
     */
    if (!mqttClient) {
        fprintf(stderr, "MQTT Client Creation failed with error %s\n",
                Aws::Crt::ErrorDebugString(mqttClient.LastError()));
        exit(-1);
    }

    /*
     * Now create a connection object. Note: This type is move only
     * and its underlying memory is managed by the client.
     */
    auto connection = mqttClient.NewConnection(clientConfig);

    if (!connection) {
        fprintf(stderr, "MQTT Connection Creation failed with error %s\n",
                Aws::Crt::ErrorDebugString(mqttClient.LastError()));
        exit(-1);
    }

    /*
     * In a real world application you probably don't want to enforce
     * synchronous behavior but this is a sample console application, so we'll
     * just do that with a condition variable.
     */
    std::promise<bool> connectionCompletedPromise;
    std::promise<void> connectionClosedPromise;

    /*
     * This will execute when an mqtt connect has completed or failed.
     */
    auto onConnectionCompleted = [&](Aws::Crt::Mqtt::MqttConnection &,
                                     int errorCode,
                                     Aws::Crt::Mqtt::ReturnCode returnCode,
                                     bool) {
        if (errorCode) {
            fprintf(stdout, "Connection failed with error %s\n",
                    Aws::Crt::ErrorDebugString(errorCode));
            connectionCompletedPromise.set_value(false);
        } else {
            if (returnCode != AWS_MQTT_CONNECT_ACCEPTED) {
                fprintf(stdout, "Connection failed with mqtt return code %d\n",
                        (int)returnCode);
                connectionCompletedPromise.set_value(false);
            } else {
                printf("Connection completed successfully.");
                connectionCompletedPromise.set_value(true);
            }
        }
    };

    auto onInterrupted = [&](Aws::Crt::Mqtt::MqttConnection &, int error) {
        fprintf(stdout, "Connection interrupted with error %s\n",
                Aws::Crt::ErrorDebugString(error));
    };

    auto onResumed = [&](Aws::Crt::Mqtt::MqttConnection &,
                         Aws::Crt::Mqtt::ReturnCode,
                         bool) { fprintf(stdout, "Connection resumed\n"); };

    /*
     * Invoked when a disconnect message has completed.
     */
    auto onDisconnect = [&](Aws::Crt::Mqtt::MqttConnection &) {
        {
            fprintf(stdout, "Disconnect completed\n");
            connectionClosedPromise.set_value();
        }
    };

    connection->OnConnectionCompleted = std::move(onConnectionCompleted);
    connection->OnDisconnect = std::move(onDisconnect);
    connection->OnConnectionInterrupted = std::move(onInterrupted);
    connection->OnConnectionResumed = std::move(onResumed);

    connection->SetOnMessageHandler([](Aws::Crt::Mqtt::MqttConnection &,
                                       const Aws::Crt::String &topic,
                                       const Aws::Crt::ByteBuf &payload) {
        fprintf(stdout, "Generic Publish received on topic %s, payload:\n",
                topic.c_str());
        fwrite(payload.buffer, 1, payload.len, stdout);
        printf("\n");
    });

    /*
     * Actually perform the connect dance.
     * This will use default ping behavior of 1 hour and 3 second timeouts.
     * If you want different behavior, those arguments go into slots 3 & 4.
     */
    printf("Connecting...\n");
    if (!connection->Connect(clientId.c_str(), false, 1000)) {
        fprintf(stderr, "MQTT Connection failed with error %s\n",
                Aws::Crt::ErrorDebugString(connection->LastError()));
        exit(-1);
    }

    if (connectionCompletedPromise.get_future().get()) {
        /*
         * This is invoked upon the receipt of a Publish on a subscribed topic.
         */
        auto onPublish = [&](Aws::Crt::Mqtt::MqttConnection &,
                             const Aws::Crt::String &topic,
                             const Aws::Crt::ByteBuf &byteBuf) {
            fprintf(stdout, "Publish received on topic %s\n", topic.c_str());
            fprintf(stdout, "\n Message:\n");
            fwrite(byteBuf.buffer, 1, byteBuf.len, stdout);
            fprintf(stdout, "\n");
        };

        /*
         * Subscribe for incoming publish messages on topic.
         */
        std::promise<void> subscribeFinishedPromise;
        auto onSubAck = [&](Aws::Crt::Mqtt::MqttConnection &, uint16_t packetId,
                            const Aws::Crt::String &topic,
                            Aws::Crt::Mqtt::QOS QoS, int errorCode) {
            if (errorCode) {
                fprintf(stderr, "Subscribe failed with error %s\n",
                        aws_error_debug_str(errorCode));
                exit(-1);
            } else {
                if (!packetId || QoS == AWS_MQTT_QOS_FAILURE) {
                    fprintf(stderr, "Subscribe rejected by the broker.");
                    exit(-1);
                } else {
                    fprintf(stdout,
                            "Subscribe on topic %s on packetId %d Succeeded\n",
                            topic.c_str(), packetId);
                }
            }
            subscribeFinishedPromise.set_value();
        };

        connection->Subscribe(topic.c_str(), AWS_MQTT_QOS_AT_LEAST_ONCE,
                              onPublish, onSubAck);
        subscribeFinishedPromise.get_future().wait();

        /**
         * send payload
         */
        char rp[62] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a',
                       'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
                       'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w',
                       'x', 'y', 'z', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
                       'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
                       'T', 'U', 'V', 'W', 'X', 'Y', 'Z'};
        char tmp[100];
        srand(time(NULL));
        for (int i = 0; i < 100; i++) {
            tmp[i] = rp[rand() % 62];
        }
        Aws::Crt::String input = tmp;

        Aws::Crt::ByteBuf payload = Aws::Crt::ByteBufNewCopy(
            Aws::Crt::DefaultAllocator(), (const uint8_t *)input.data(),
            input.length());
        Aws::Crt::ByteBuf *payloadPtr = &payload;

        auto onPublishComplete = [payloadPtr](Aws::Crt::Mqtt::MqttConnection &,
                                              uint16_t packetId,
                                              int errorCode) {
            aws_byte_buf_clean_up(payloadPtr);

            if (packetId) {
                fprintf(stdout, "Operation on packetId %d Succeeded\n",
                        packetId);
            } else {
                fprintf(stdout, "Operation failed with error %s\n",
                        aws_error_debug_str(errorCode));
            }
        };
        connection->Publish(topic.c_str(), AWS_MQTT_QOS_AT_LEAST_ONCE, false,
                            payload, onPublishComplete);

        /*
         * Unsubscribe from the topic.
         */
        std::promise<void> unsubscribeFinishedPromise;
        connection->Unsubscribe(
            topic.c_str(),
            [&](Aws::Crt::Mqtt::MqttConnection &, uint16_t, int) {
                unsubscribeFinishedPromise.set_value();
            });
        unsubscribeFinishedPromise.get_future().wait();
    }

    /* Disconnect */
    if (connection->Disconnect()) {
        connectionClosedPromise.get_future().wait();
    }
}

int main(int argc, char *argv[]) {
    test();
    return 0;
}
