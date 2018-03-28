// hello-aws-sdk.cpp : Defines the entry point for the console application.

#include <aws/core/Aws.h>
#include <aws/sns/SNSClient.h>
#include <aws/sns/model/CreateTopicRequest.h>
#include <aws/sns/model/SubscribeRequest.h>
#include <aws/sns/model/PublishRequest.h>
#include <aws/sqs/SQSClient.h>
#include <aws/sqs/model/CreateQueueRequest.h>
#include <aws/sqs/model/GetQueueAttributesRequest.h>
#include <aws/sqs/model/SetQueueAttributesRequest.h>
#include <aws/sqs/model/ReceiveMessageRequest.h>
#include <aws/sqs/model/DeleteMessageRequest.h>

#include <atomic>
#include <cassert>

// https://github.com/markcallen/snssqs/blob/master/create.js

typedef std::function<void()> Callback;


template<typename T, typename... Args>
auto series(T f, Args... xs)
{
    return std::bind(f, Callback(series(xs...)));
}

template<typename T>
auto series(T f)
{
    return f;
}


auto& sns()
{
    static Aws::SNS::SNSClient client;
    return client;
}

auto& sqs()
{
    static Aws::SQS::SQSClient client;
    return client;
}

Aws::String topicArn;
Aws::String queueUrl;
Aws::String queueArn;


void createTopic(Callback cb) 
{
    using namespace Aws::SNS;
    sns().CreateTopicAsync(Model::CreateTopicRequest().WithName("demo"), 
        [cb](const SNSClient*, 
            const Model::CreateTopicRequest& request, 
            const Model::CreateTopicOutcome& outcome, 
            const std::shared_ptr<const Aws::Client::AsyncCallerContext>&) 
        {
            if (outcome.IsSuccess()) {
                topicArn = outcome.GetResult().GetTopicArn();
                cb();
            }
        });
}

void createQueue(Callback cb) 
{
    using namespace Aws::SQS;
    sqs().CreateQueueAsync(Model::CreateQueueRequest().WithQueueName("demo"),
        [cb](const SQSClient*, 
            const Model::CreateQueueRequest&, 
            const Model::CreateQueueOutcome& outcome, 
            const std::shared_ptr<const Aws::Client::AsyncCallerContext>&)
        {
            if (outcome.IsSuccess()) {
                queueUrl = outcome.GetResult().GetQueueUrl();
                cb();
            }
        });
}

void getQueueAttr(Callback cb) 
{
    using namespace Aws::SQS;
    sqs().GetQueueAttributesAsync(
        Model::GetQueueAttributesRequest().WithQueueUrl(queueUrl).WithAttributeNames({ Model::QueueAttributeName::QueueArn }),
        [cb](const SQSClient*, 
            const Model::GetQueueAttributesRequest&, 
            const Model::GetQueueAttributesOutcome& outcome, 
            const std::shared_ptr<const Aws::Client::AsyncCallerContext>&) 
        {
            if (outcome.IsSuccess()) {
                const auto attr = outcome.GetResult().GetAttributes();
                auto it = attr.find(Model::QueueAttributeName::QueueArn);
                if (it != attr.end()) {
                    queueArn = it->second;
                    cb();
                }
            }
        });
}

void snsSubscribe(Callback cb) 
{
    using namespace Aws::SNS;
    sns().SubscribeAsync(
        Model::SubscribeRequest().WithTopicArn(topicArn).WithProtocol("sqs").WithEndpoint(queueArn),
        [cb](const SNSClient*, 
            const Model::SubscribeRequest&, 
            const Model::SubscribeOutcome& outcome, 
            const std::shared_ptr<const Aws::Client::AsyncCallerContext>&) 
        {
            if (outcome.IsSuccess()) {
                cb();
            }
        });
}

void setQueueAttr(Callback cb) 
{
    using namespace Aws::SQS;
    using namespace std::chrono;
    const long long now = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    char attributes[1024];

    snprintf(attributes, sizeof(attributes) / sizeof(attributes[0]),
        R"({
    "Version": "2008-10-17",
    "Id": "%s/SQSDefaultPolicy",
    "Statement": [{
      "Sid": "Sid%lld",
      "Effect": "Allow",
      "Principal": {
        "AWS": "*"
      },
      "Action": "SQS:SendMessage",
      "Resource": "%s",
      "Condition": {
        "ArnEquals": {
          "aws:SourceArn": "%s"
        }
      }
    }
    ]
})", queueArn.c_str(), now, queueArn.c_str(), topicArn.c_str());

    sqs().SetQueueAttributesAsync(
        Model::SetQueueAttributesRequest().WithQueueUrl(queueUrl).WithAttributes({ {Model::QueueAttributeName::Policy, attributes} }),
        [cb](const SQSClient*, 
            const Model::SetQueueAttributesRequest&, 
            const Model::SetQueueAttributesOutcome& outcome, 
            const std::shared_ptr<const Aws::Client::AsyncCallerContext>&) 
        {
            if (outcome.IsSuccess())
            {
                cb();
            }
        });
}

//////////////////////////////////////////////////////////////////////////////

void getMessages(std::atomic_bool& stop, std::shared_ptr<std::promise<void>> stopped)
{
    using namespace Aws::SQS;
    sqs().ReceiveMessageAsync(Model::ReceiveMessageRequest().WithQueueUrl(queueUrl).WithMaxNumberOfMessages(10),
        [&stop, stopped](const SQSClient*,
            const Model::ReceiveMessageRequest&, 
            const Model::ReceiveMessageOutcome& outcome, 
            const std::shared_ptr<const Aws::Client::AsyncCallerContext>&) 
        {
            const auto& messages = outcome.GetResult().GetMessages();
            if (messages.empty() && stop)
            {
                return;
            }
            for (const auto& message : messages)
            {
                std::cout << "Message received: " << message.GetBody() << '\n';
                // TODO handle
                sqs().DeleteMessageAsync(
                    Model::DeleteMessageRequest().WithQueueUrl(queueUrl).WithReceiptHandle(message.GetReceiptHandle()),
                    [stopped](const SQSClient*,
                        const Model::DeleteMessageRequest&, 
                        const Model::DeleteMessageOutcome& outcome, 
                        const std::shared_ptr<const Aws::Client::AsyncCallerContext>&) 
                    {
                        assert(outcome.IsSuccess());
                        // TODO handle
                    });
            }
            getMessages(stop, stopped);
        });
}

auto publish(const Aws::String& mesg) 
{
    using namespace Aws::SNS;

    auto published = std::make_shared<std::promise<void>>();
    sns().PublishAsync(Model::PublishRequest().WithTopicArn(topicArn).WithMessage(mesg),
        [published](const SNSClient*, 
            const Model::PublishRequest&, 
            const Model::PublishOutcome& outcome, 
            const std::shared_ptr<const Aws::Client::AsyncCallerContext>&) 
        {
            if (outcome.IsSuccess()) {
                published->set_value();
            }
        });

    return published->get_future();
}

//////////////////////////////////////////////////////////////////////////////

int main()
{
    try
    {
        Aws::SDKOptions options;
        Aws::InitAPI(options);

        {
            auto created = std::make_shared<std::promise<void>>();
            auto fut = created->get_future();
            series(createTopic, createQueue, getQueueAttr, snsSubscribe, setQueueAttr, [created] { created->set_value(); })();
            created.reset();
            fut.get();
        }

        std::atomic_bool stop;
        auto stopped = std::shared_ptr<std::promise<void>>(
            new std::promise<void>, [](std::promise<void>* p) { p->set_value(); delete p; });
        auto fut = stopped->get_future();
        getMessages(stop, stopped);
        stopped.reset();

        for (int i = 1; i <= 100; ++i)
        {
            std::ostringstream s;
            s << "message: " << i;
            publish(s.str().c_str()).get();
        }

        stop = true;
        fut.get();

        Aws::ShutdownAPI(options);
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << '\n';
        return 1;
    }
    return 0;
}
