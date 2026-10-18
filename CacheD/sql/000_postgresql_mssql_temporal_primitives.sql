-- PostgreSQL primitives for the SQL Server temporal types used by CacheD.
--
-- SQL Server GETDATE() returns the current local database-server time as a
-- legacy DATETIME value.  PostgreSQL CURRENT_TIMESTAMP/LOCALTIMESTAMP instead
-- freeze at transaction start and keep microseconds, so neither is a valid
-- direct replacement for production procedures that can share a transaction.
--
-- DATETIME is represented by its externally visible millisecond values
-- (.000, .003 and .007 in each 10 ms cycle).  That keeps the equality,
-- ordering and ODBC-visible values used by CacheD while keeping PostgreSQL's
-- timezone-free timestamp storage.  SMALLDATETIME keeps its documented
-- 29.998/29.999-second minute boundary and restricted date range.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."mssql_datetime"(
    p_value timestamp without time zone
)
RETURNS timestamp without time zone
LANGUAGE plpgsql
IMMUTABLE
STRICT
PARALLEL SAFE
AS $function$
DECLARE
    v_day timestamp without time zone;
    v_ticks numeric;
    v_milliseconds numeric;
    v_result timestamp without time zone;
BEGIN
    IF NOT isfinite(p_value)
       OR p_value < timestamp '1753-01-01 00:00:00'
       OR p_value >= timestamp '10000-01-01 00:00:00' THEN
        RAISE EXCEPTION USING
            ERRCODE = '22008',
            MESSAGE = format(
                'timestamp %s is outside SQL Server datetime range',
                p_value
            );
    END IF;

    v_day := date_trunc('day', p_value);
    v_ticks := round(
        extract(epoch FROM (p_value - v_day))::numeric * 300
    );
    v_milliseconds := round(v_ticks * 10 / 3);
    v_result := v_day
        + interval '1 millisecond' * v_milliseconds::double precision;

    IF v_result < timestamp '1753-01-01 00:00:00'
       OR v_result > timestamp '9999-12-31 23:59:59.997' THEN
        RAISE EXCEPTION USING
            ERRCODE = '22008',
            MESSAGE = format(
                'timestamp %s rounds outside SQL Server datetime range',
                p_value
            );
    END IF;

    RETURN v_result;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."mssql_smalldatetime"(
    p_value timestamp without time zone
)
RETURNS timestamp without time zone
LANGUAGE plpgsql
IMMUTABLE
STRICT
PARALLEL SAFE
AS $function$
DECLARE
    v_minute timestamp without time zone;
    v_result timestamp without time zone;
BEGIN
    IF NOT isfinite(p_value)
       OR p_value < timestamp '1900-01-01 00:00:00'
       OR p_value >= timestamp '2079-06-07 00:00:00' THEN
        RAISE EXCEPTION USING
            ERRCODE = '22008',
            MESSAGE = format(
                'timestamp %s is outside SQL Server smalldatetime range',
                p_value
            );
    END IF;

    v_minute := date_trunc('minute', p_value);
    v_result := v_minute;
    IF p_value - v_minute >= interval '29.999 seconds' THEN
        v_result := v_result + interval '1 minute';
    END IF;

    IF v_result < timestamp '1900-01-01 00:00:00'
       OR v_result > timestamp '2079-06-06 23:59:00' THEN
        RAISE EXCEPTION USING
            ERRCODE = '22008',
            MESSAGE = format(
                'timestamp %s rounds outside SQL Server smalldatetime range',
                p_value
            );
    END IF;

    RETURN v_result;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."mssql_getdate"()
RETURNS timestamp without time zone
LANGUAGE sql
VOLATILE
AS $function$
    SELECT "world"."mssql_datetime"(
        clock_timestamp()::timestamp without time zone
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."mssql_getsmalldatetime"()
RETURNS timestamp without time zone
LANGUAGE sql
VOLATILE
AS $function$
    SELECT "world"."mssql_smalldatetime"(
        "world"."mssql_getdate"()
    );
$function$;

COMMIT;
