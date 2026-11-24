// Qt 6.8 的 QSqlDatabase 要求先存在 QCoreApplication，Catch2 默认 main 不提供。
#include <QCoreApplication>

#include <catch2/catch_session.hpp>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
