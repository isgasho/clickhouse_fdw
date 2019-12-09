#pragma once

namespace clickhouse {

    /// То, что передаёт сервер.
    namespace ServerCodes {
        enum {
            Hello        = 0,    /// Имя, версия, ревизия.
            Data         = 1,    /// Блок данных со сжатием или без.
            Exception    = 2,    /// Исключение во время обработки запроса.
            Progress     = 3,    /// Прогресс выполнения запроса: строк считано, байт считано.
            Pong         = 4,    /// Ответ на Ping.
            EndOfStream  = 5,    /// Все пакеты были переданы.
            ProfileInfo  = 6,    /// Пакет с профайлинговой информацией.
            Totals       = 7,    /// Блок данных с тотальными значениями, со сжатием или без.
            Extremes     = 8,    /// Блок данных с минимумами и максимумами, аналогично.
            TablesStatusResponse = 9, /// A response to TablesStatus request.
            Log			 = 10,   /// System logs of the query execution
            TableColumns = 11    /// Columns' description for default values calculation
        };
    }

    /// То, что передаёт клиент.
    namespace ClientCodes {
        enum {
            Hello       = 0,    /// Имя, версия, ревизия, БД по-умолчанию.
            Query       = 1,    /** Идентификатор запроса, настройки на отдельный запрос,
                                  * информация, до какой стадии исполнять запрос,
                                  * использовать ли сжатие, текст запроса (без данных для INSERT-а).
                                  */
            Data        = 2,    /// Блок данных со сжатием или без.
            Cancel      = 3,    /// Отменить выполнение запроса.
            Ping        = 4,    /// Проверка живости соединения с сервером.
        };
    }

    /// Использовать ли сжатие.
    namespace CompressionState {
        enum {
            Disable     = 0,
            Enable      = 1,
        };
    }

    namespace Stages {
        enum {
            Complete    = 2,
        };
    }
}