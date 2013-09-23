#include "robomongo/core/mongodb/MongoWorker.h"

#include <QThread>
#include <QTimer>
#include <boost/scoped_ptr.hpp>
#include <mongo/scripting/engine_spidermonkey.h>

#include "robomongo/core/events/MongoEvents.h"
#include "robomongo/core/engine/ScriptEngine.h"
#include "robomongo/core/EventBus.h"
#include "robomongo/core/AppRegistry.h"
#include "robomongo/core/mongodb/MongoClient.h"
#include "robomongo/core/settings/ConnectionSettings.h"
#include "robomongo/core/domain/MongoShellResult.h"
#include "robomongo/core/domain/MongoCollectionInfo.h"
#include "robomongo/core/settings/CredentialSettings.h"

namespace Robomongo
{
    MongoWorker::MongoWorker(ConnectionSettings *connection, QObject *parent) : QObject(parent),
        _connection(connection),
        _scriptEngine(NULL),
        _dbclient(NULL),
        _address(connection->getFullAddress())
    {
        init();
    }

    MongoWorker::~MongoWorker()
    {
        delete _dbclient;
        delete _connection;
        _thread->quit();
        if (!_thread->wait(2000))
            _thread->terminate();

        delete _scriptEngine;
        delete _thread;
    }

    /**
     * @brief Initialise MongoWorker
     */
    void MongoWorker::init()
    {
        _isAdmin = true;
        _thread = new QThread(this);
        moveToThread(_thread);

        _thread->start();
    }

    void MongoWorker::handle(InitRequest *event)
    {
        try {
            _scriptEngine = new ScriptEngine(_connection);
            _scriptEngine->init(event->isLoadMongoRcJs());
            _scriptEngine->use(_connection->defaultDatabase());
            _scriptEngine->setBatchSize(event->batchSize());

            _keepAliveTimer = new QTimer(this);
            connect(_keepAliveTimer, SIGNAL(timeout()), this, SLOT(keepAlive()));
            _keepAliveTimer->start(60 * 1000); // every minute
        }
        catch (const std::exception &ex) {
            reply(event->sender(), new InitResponse(this, EventError(ex.what())));
        }
    }

    void MongoWorker::handle(FinalizeRequest *event)
    {
    }

    /**
     * @brief Initiate connection to MongoDB
     */
    void MongoWorker::handle(EstablishConnectionRequest *event)
    {
        QMutexLocker lock(&_firstConnectionMutex);

        try {
            mongo::DBClientBase *conn = getConnection();

            if (_connection->hasEnabledPrimaryCredential()) {
                std::string errmsg;
                bool ok = conn->auth(
                    _connection->primaryCredential()->databaseName(),
                    _connection->primaryCredential()->userName(),
                    _connection->primaryCredential()->userPassword(), errmsg);

                if (!ok) {
                    throw std::runtime_error("Unable to authorize");
                }

                // If authentication succeed and database name is 'admin' -
                // then user is admin, otherwise user is not admin
                std::string dbName = _connection->primaryCredential()->databaseName();
                std::transform(dbName.begin(), dbName.end(), dbName.begin(), ::tolower);
                if (dbName.compare("admin") != 0) // dbName is NOT "admin"
                    _isAdmin = false;

                // Save name of db on which we authenticated
                _authDatabase = _connection->primaryCredential()->databaseName();
            }
            boost::scoped_ptr<MongoClient> client(getClient());
            //conn->done();
            std::vector<std::string> dbNames = client->getDatabaseNames();
            reply(event->sender(), new EstablishConnectionResponse(this, ConnectionInfo(_address,dbNames,client->getVersion()) ));
        } catch(const std::exception &ex) {
            reply(event->sender(), new EstablishConnectionResponse(this, EventError(ex.what())));
        }
    }

    /**
     * @brief Load list of all database names
     */
    void MongoWorker::handle(LoadDatabaseNamesRequest *event)
    {
        try {
            // If user not an admin - he doesn't have access to mongodb 'listDatabases' command
            // Non admin user has access only to the single database he specified while performing auth.
            if (!_isAdmin) {
                std::vector<std::string> dbNames;
                dbNames.push_back(_authDatabase);
                reply(event->sender(), new LoadDatabaseNamesResponse(this, dbNames));
                return;
            }

            boost::scoped_ptr<MongoClient> client(getClient());
            std::vector<std::string> dbNames = client->getDatabaseNames();
            client->done();

            reply(event->sender(), new LoadDatabaseNamesResponse(this, dbNames));
        } catch(const mongo::DBException &) {
            reply(event->sender(), new LoadDatabaseNamesResponse(this, EventError("Unable to load database names.")));
        }
    }

    /**
     * @brief Load list of all collection names
     */
    void MongoWorker::handle(LoadCollectionNamesRequest *event)
    {
        try {
            boost::scoped_ptr<MongoClient> client(getClient());

            std::vector<std::string> stringList = client->getCollectionNames(event->databaseName());
            const std::vector<MongoCollectionInfo> &infos = client->runCollStatsCommand(stringList);
            client->done();

            reply(event->sender(), new LoadCollectionNamesResponse(this, event->databaseName(), infos));
        } catch(const mongo::DBException &) {
            reply(event->sender(), new LoadCollectionNamesResponse(this, EventError("Unable to load list of collections.")));
        }
    }

    void MongoWorker::handle(LoadUsersRequest *event)
    {
        try {
            boost::scoped_ptr<MongoClient> client(getClient());
            const std::vector<MongoUser> &users = client->getUsers(event->databaseName());
            client->done();

            reply(event->sender(), new LoadUsersResponse(this, event->databaseName(), users));
        } catch(const mongo::DBException &) {
            reply(event->sender(), new LoadUsersResponse(this, EventError("Unable to load list of users.")));
        }
    }

    void MongoWorker::handle(LoadCollectionIndexesRequest *event)
    {
        try {
            boost::scoped_ptr<MongoClient> client(getClient());
            const std::vector<EnsureIndexInfo> &ind = client->getIndexes(event->collection());
            client->done();

            reply(event->sender(), new LoadCollectionIndexesResponse(this, ind));
        } catch(const mongo::DBException &) {
            reply(event->sender(), new LoadCollectionIndexesResponse(this, std::vector<EnsureIndexInfo>()));
        }
    }

    void MongoWorker::handle(EnsureIndexRequest *event)
    {
        const EnsureIndexInfo &newInfo = event->newInfo();
        const EnsureIndexInfo &oldInfo = event->oldInfo();
        try {
            boost::scoped_ptr<MongoClient> client(getClient());
            client->ensureIndex(oldInfo,newInfo);
            const std::vector<EnsureIndexInfo> &ind = client->getIndexes(newInfo._collection);
            client->done();

            reply(event->sender(), new LoadCollectionIndexesResponse(this, ind));
        } catch(const mongo::DBException &) {
            reply(event->sender(), new LoadCollectionIndexesResponse(this, std::vector<EnsureIndexInfo>()));
        }
    }

    void MongoWorker::handle(DropCollectionIndexRequest *event)
    {
        try {
            boost::scoped_ptr<MongoClient> client(getClient());
            client->dropIndexFromCollection(event->collection(),event->name());
            client->done();
            reply(event->sender(), new DeleteCollectionIndexResponse(this, event->collection(), event->name()));
        } catch(const mongo::DBException &) {
            reply(event->sender(), new DeleteCollectionIndexResponse(this, event->collection(), std::string() ));
        }            
    }

    void MongoWorker::handle(EditIndexRequest *event)
    {
        try {
            boost::scoped_ptr<MongoClient> client(getClient());
            client->renameIndexFromCollection(event->collection(),event->oldIndex(),event->newIndex());
            const std::vector<EnsureIndexInfo> &ind = client->getIndexes(event->collection());
            client->done();

            reply(event->sender(), new LoadCollectionIndexesResponse(this, ind));
        } catch(const mongo::DBException &) {
            reply(event->sender(), new LoadCollectionIndexesResponse(this, std::vector<EnsureIndexInfo>()));
        } 
    }

    void MongoWorker::handle(LoadFunctionsRequest *event)
    {
        try {
            boost::scoped_ptr<MongoClient> client(getClient());
            const std::vector<MongoFunction> &funs = client->getFunctions(event->databaseName());
            client->done();

            reply(event->sender(), new LoadFunctionsResponse(this, event->databaseName(), funs));
        } catch(const mongo::DBException &) {
            reply(event->sender(), new LoadFunctionsResponse(this, EventError("Unable to load list of functions.")));
        }
    }

    void MongoWorker::handle(InsertDocumentRequest *event)
    {
        try {
            boost::scoped_ptr<MongoClient> client(getClient());

            if (event->overwrite())
                client->saveDocument(event->obj(), event->database(), event->collection());
            else
                client->insertDocument(event->obj(), event->database(), event->collection());

            client->done();

            reply(event->sender(), new InsertDocumentResponse(this));
        } catch(const mongo::DBException &) {
            reply(event->sender(), new InsertDocumentResponse(this, EventError("Unable to insert document.")));
        }
    }

    void MongoWorker::handle(RemoveDocumentRequest *event)
    {
        try {
            boost::scoped_ptr<MongoClient> client(getClient());

            client->removeDocuments(event->database(), event->collection(), event->query(), event->justOne());
            client->done();

            reply(event->sender(), new RemoveDocumentResponse(this));
        } catch(const mongo::DBException &) {
            reply(event->sender(), new RemoveDocumentResponse(this, EventError("Unable to remove documents.")));
        }
    }

    void MongoWorker::handle(ExecuteQueryRequest *event)
    {
        try {
            boost::scoped_ptr<MongoClient> client(getClient());
            std::vector<MongoDocumentPtr> docs = client->query(event->queryInfo());
            client->done();

            reply(event->sender(), new ExecuteQueryResponse(this, event->resultIndex(), event->queryInfo(), docs));
        } catch(const mongo::DBException &) {
            reply(event->sender(), new ExecuteQueryResponse(this, EventError("Unable to complete query.")));
        }
    }

    /**
     * @brief Execute javascript
     */
    void MongoWorker::handle(ExecuteScriptRequest *event)
    {
        try {
            MongoShellExecResult result = _scriptEngine->exec(event->script, event->databaseName);
            reply(event->sender(), new ExecuteScriptResponse(this, result, event->script.empty()));
        } catch(const mongo::DBException &) {
            reply(event->sender(), new ExecuteScriptResponse(this, EventError("Unable to complete query.")));
        }
    }

    void MongoWorker::handle(AutocompleteRequest *event)
    {
        try {
            QStringList list = _scriptEngine->complete(event->prefix);
            reply(event->sender(), new AutocompleteResponse(this, list, event->prefix));
        } catch(const mongo::DBException &) {
            reply(event->sender(), new ExecuteScriptResponse(this, EventError("Unable to autocomplete query.")));
        }
    }

    void MongoWorker::handle(CreateDatabaseRequest *event)
    {
        try {
            boost::scoped_ptr<MongoClient> client(getClient());
            client->createDatabase(event->database());
            client->done();

            reply(event->sender(), new CreateDatabaseResponse(this));
        } catch(const mongo::DBException &) {
            reply(event->sender(), new CreateDatabaseResponse(this, EventError("Unable to create database.")));
        }
    }

    void MongoWorker::handle(DropDatabaseRequest *event)
    {
        try {
            boost::scoped_ptr<MongoClient> client(getClient());
            client->dropDatabase(event->database());
            client->done();

            reply(event->sender(), new DropDatabaseResponse(this));
        } catch(const mongo::DBException &) {
            reply(event->sender(), new DropDatabaseResponse(this, EventError("Unable to drop database.")));
        }
    }

    void MongoWorker::handle(CreateCollectionRequest *event)
    {
        try {
            boost::scoped_ptr<MongoClient> client(getClient());
            client->createCollection(event->database(), event->collection());
            client->done();

            reply(event->sender(), new CreateCollectionResponse(this));
        } catch(const mongo::DBException &) {
            reply(event->sender(), new CreateCollectionResponse(this, EventError("Unable to create collection.")));
        }
    }

    void MongoWorker::handle(DropCollectionRequest *event)
    {
        try {
            boost::scoped_ptr<MongoClient> client(getClient());
            client->dropCollection(event->database(), event->collection());
            client->done();

            reply(event->sender(), new DropCollectionResponse(this));
        } catch(const mongo::DBException &) {
            reply(event->sender(), new DropCollectionResponse(this, EventError("Unable to drop collection.")));
        }
    }

    void MongoWorker::handle(RenameCollectionRequest *event)
    {
        try {
            boost::scoped_ptr<MongoClient> client(getClient());
            client->renameCollection(event->database(), event->collection(), event->newCollection());
            client->done();

            reply(event->sender(), new RenameCollectionResponse(this));
        } catch(const mongo::DBException &) {
            reply(event->sender(), new RenameCollectionResponse(this, EventError("Unable to rename collection.")));
        }
    }

    void MongoWorker::handle(DuplicateCollectionRequest *event)
    {
        try {
            boost::scoped_ptr<MongoClient> client(getClient());
            client->duplicateCollection(event->database(), event->collection(), event->newCollection());
            client->done();

            reply(event->sender(), new DuplicateCollectionResponse(this));
        } catch(const mongo::DBException &) {
            reply(event->sender(), new DuplicateCollectionResponse(this, EventError("Unable to duplicate collection.")));
        }
    }

    void MongoWorker::handle(CopyCollectionToDiffServerRequest *event)
    {
        try {
            boost::scoped_ptr<MongoClient> client(getClient());
            MongoNamespace from(event->databaseFrom(), event->collection());
            MongoNamespace to(event->databaseTo(), event->collection());
            MongoWorker *cl = event->worker();
            client->copyCollectionToDiffServer(cl->_dbclient,from,to);
            client->done();

            reply(event->sender(), new CopyCollectionToDiffServerResponse(this));
        } catch(const mongo::DBException &) {
            reply(event->sender(), new CopyCollectionToDiffServerResponse(this, EventError("Unable to copy collection.")));
        }
    }

    void MongoWorker::handle(CreateUserRequest *event)
    {
        try {
            boost::scoped_ptr<MongoClient> client(getClient());
            client->createUser(event->database(), event->user(), event->overwrite());
            client->done();

            reply(event->sender(), new CreateUserResponse(this));
        } catch(const mongo::DBException &) {
            reply(event->sender(), new CreateUserResponse(this, EventError("Unable to create/ovewrite user.")));
        }
    }

    void MongoWorker::handle(DropUserRequest *event)
    {
        try {
            boost::scoped_ptr<MongoClient> client(getClient());
            client->dropUser(event->database(), event->id());
            client->done();

            reply(event->sender(), new DropUserResponse(this));
        } catch(const mongo::DBException &) {
            reply(event->sender(), new DropUserResponse(this, EventError("Unable to drop user.")));
        }
    }

    void MongoWorker::handle(CreateFunctionRequest *event)
    {
        try {
            boost::scoped_ptr<MongoClient> client(getClient());
            client->createFunction(event->database(), event->function(), event->existingFunctionName());
            client->done();

            reply(event->sender(), new CreateFunctionResponse(this));
        } catch(const mongo::DBException &) {
            reply(event->sender(), new CreateFunctionResponse(this, EventError("Unable to create/ovewrite function.")));
        }
    }

    void MongoWorker::handle(DropFunctionRequest *event)
    {
        try {
            boost::scoped_ptr<MongoClient> client(getClient());
            client->dropFunction(event->database(), event->name());
            client->done();

            reply(event->sender(), new DropFunctionResponse(this));
        } catch(const mongo::DBException &) {
            reply(event->sender(), new DropFunctionResponse(this, EventError("Unable to drop function.")));
        }
    }

    mongo::DBClientBase *MongoWorker::getConnection()
    {
        if (!_dbclient) {
            mongo::DBClientConnection *conn = new mongo::DBClientConnection(true);
            conn->connect(_address);
            _dbclient = conn;
        }
        return _dbclient;
    }

    MongoClient *MongoWorker::getClient()
    {
        return new MongoClient(getConnection());
    }

    /**
     * @brief Send event to this MongoWorker
     */
    void MongoWorker::send(Event *event)
    {
        AppRegistry::instance().bus()->send(this, event);
    }

    void MongoWorker::keepAlive()
    {
        try {
            if (_dbclient) {
                // Building { ping: 1 }
                mongo::BSONObjBuilder command;
                command.append("ping", 1);
                mongo::BSONObj result;

                if (_authDatabase.empty()) {
                    _dbclient->runCommand("admin", command.obj(), result);
                } else {
                    _dbclient->runCommand(_authDatabase, command.obj(), result);
                }
            }

            if (_scriptEngine) {
                _scriptEngine->ping();
            }

        } catch(std::exception &) {
            // nothing here
        }
    }

    /**
     * @brief Send reply event to object 'receiver'
     */
    void MongoWorker::reply(QObject *receiver, Event *event)
    {
        AppRegistry::instance().bus()->send(receiver, event);
    }
}
