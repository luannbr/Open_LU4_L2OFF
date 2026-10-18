-- Current Cached.exe extended slots 454-460: item-lock mutation, item reuse,
-- the newer pledge-item manipulation path and builder GM announcements.
-- Existing item/reuse procedures already cover the first three paths.
--
-- Current machine code fixes the three GM-announcement procedure names,
-- parameter order and seven-column fetch order. No searched Classic/287/
-- Ertheia SQL tree or supplied l2dbs backup keeps their definitions. The
-- table name below is therefore an explicit portable storage reconstruction,
-- not a description of the unavailable MSSQL base-table identifier.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE TABLE IF NOT EXISTS "world"."cached_portable_gm_announce" (
    "announce_id" integer NOT NULL,
    "line_number" integer NOT NULL,
    "message" text NOT NULL,
    "start_date" integer NOT NULL,
    "end_date" integer NOT NULL,
    "period" integer NOT NULL,
    "location" integer NOT NULL,
    CONSTRAINT "pk_cached_portable_gm_announce"
        PRIMARY KEY ("announce_id", "line_number")
);

CREATE OR REPLACE FUNCTION "world"."lin_getgmannounce"()
RETURNS TABLE (
    announce_id integer,
    message text,
    line_number integer,
    start_date integer,
    end_date integer,
    period integer,
    location integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT announce."announce_id",
           announce."message",
           announce."line_number",
           announce."start_date",
           announce."end_date",
           announce."period",
           announce."location"
      FROM "world"."cached_portable_gm_announce" AS announce
     ORDER BY announce."announce_id", announce."line_number";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_delgmannounce"(
    p_announce_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."cached_portable_gm_announce" AS announce
     WHERE announce."announce_id" = p_announce_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setgmannounce"(
    p_announce_id integer,
    p_message text,
    p_line_number integer,
    p_start_date integer,
    p_end_date integer,
    p_period integer,
    p_location integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."cached_portable_gm_announce" AS announce (
        "announce_id",
        "line_number",
        "message",
        "start_date",
        "end_date",
        "period",
        "location"
    ) VALUES (
        p_announce_id,
        p_line_number,
        COALESCE(p_message, ''),
        p_start_date,
        p_end_date,
        p_period,
        p_location
    )
    ON CONFLICT ("announce_id", "line_number") DO UPDATE
    SET "message" = EXCLUDED."message",
        "start_date" = EXCLUDED."start_date",
        "end_date" = EXCLUDED."end_date",
        "period" = EXCLUDED."period",
        "location" = EXCLUDED."location";
$function$;

COMMIT;
