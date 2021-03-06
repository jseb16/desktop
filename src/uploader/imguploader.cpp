#include "imguploader.h"
#include "../mainwindow.h"

imgUploader::imgUploader(QSettings *parentSettings, QObject *parent) :
    QObject(parent)
{
    settings = parentSettings;
    UserAgent = "tickmo";
    uploading = false;
    QTimer *timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(onUploadTimeIsCome()));
    timer->start(UPLOAD_INTERVAL);
    quitAfterUploading = false;
}

void imgUploader::uploadImage(QString filename)
{
    filesQueue.enqueue(filename);
}

void imgUploader::addTime(TimeMessage timeMessage)
{
    timeMessagesQueue.enqueue(timeMessage);
}

void imgUploader::uploadFiles(bool quitAfterAll)
{
    quitAfterUploading = quitAfterAll;
    if (filesQueue.isEmpty() && timeMessagesQueue.isEmpty()) {
        if (quitAfterUploading) {
            QApplication::quit();
        }
        return;
    }
    QUrl serviceUrl = QUrl(MainWindow::api_url("api/v1/file"));

    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QNetworkRequest request(serviceUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, UserAgent);

    QString username = settings->value("User/login").toString();
    QString token = settings->value("User/token").toString();
    QString authHeader = "Token token=\"" + token + "\", user_email=\"" + username + "\"";
    request.setRawHeader("Authorization", authHeader.toLocal8Bit());

    QJsonArray timeMessagesJson;
    while (!timeMessagesQueue.isEmpty())
    {
        TimeMessage timeMessage = timeMessagesQueue.dequeue();
        timeMessagesFailedQueue.enqueue(timeMessage);
        QJsonObject item;
        item.insert("from", QJsonValue((int) timeMessage.from));
        item.insert("to", QJsonValue((int) timeMessage.to));
        timeMessagesJson.append(item);
    }
    if (!timeMessagesJson.isEmpty()) {
        QJsonDocument doc(timeMessagesJson);
        QByteArray bytes = doc.toJson();
        QHttpPart jsonPart;
        jsonPart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/json"));
        jsonPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"messages\""));
        jsonPart.setBody(bytes);
        multiPart->append(jsonPart);
    }

    while (!filesQueue.isEmpty())
    {
        QString filename = filesQueue.dequeue();
        failedQueue.enqueue(filename);
        QFile *file = new QFile(filename);
        QFileInfo *fileInfo = new QFileInfo(filename);
        file->setParent(multiPart);
        file->open(QIODevice::ReadOnly);

        QHttpPart imagePart;
        imagePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("image/png"));
        imagePart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"screenshots[]\"; filename=\"" + fileInfo->fileName() +  "\""));

        imagePart.setBodyDevice(file);
        multiPart->append(imagePart);
    }

    request.setHeader(QNetworkRequest::ContentTypeHeader, "multipart/form-data; boundary=" + multiPart->boundary());
    currentUpload =  manager.post(request, multiPart);
    multiPart->setParent(currentUpload);

    connect(currentUpload, SIGNAL(uploadProgress(qint64,qint64)),this, SLOT(onUploadProgress(qint64,qint64)));
    connect(currentUpload, SIGNAL(finished()),this,SLOT(onUploadFinished()));

    uploading = true;
}

bool imgUploader::isUploading()
{
    return uploading;
}

void imgUploader::onUploadFinished()
{
    uploading = false;
    if (currentUpload->error() == QNetworkReply::NoError)
    {
        failedQueue.clear();
        timeMessagesFailedQueue.clear();
    }
    else
    {
        while (!failedQueue.isEmpty())
        {
            filesQueue.enqueue(failedQueue.dequeue());
        }
        while (!timeMessagesFailedQueue.isEmpty())
        {
            timeMessagesQueue.enqueue(timeMessagesFailedQueue.dequeue());
        }
    }
    currentUpload->deleteLater();
    emit uploadFinished();
    if (quitAfterUploading) {
        QApplication::quit();
    }
}

void imgUploader::onUploadProgress(qint64 uploaded, qint64 total)
{
    emit uploadProgress( uploaded,  total);
}

void imgUploader::onUploadTimeIsCome()
{
    uploadFiles();
}
