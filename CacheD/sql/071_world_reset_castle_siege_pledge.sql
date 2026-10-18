-- Current Cached.exe primary opcode 86 (ResetCastleSiegePledge) persistence.
--
-- The current handler and unnamed helper at 0x004845bc fix the one- and
-- four-input ABIs. Current L2Server 0x00648a2c fixes each
-- repeated tuple as (pledge_id, type, world_id). All compatible SQL donor
-- families have the same delete and insert effects.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_deletecastlewarbycastleid"(
    p_castle_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."castle_war" AS castle_war
    WHERE castle_war."castle_id" = p_castle_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_insertcastlewar"(
    p_castle_id integer,
    p_pledge_id integer,
    p_type integer,
    p_world_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."castle_war" (
        "castle_id",
        "pledge_id",
        "type",
        "world_id"
    ) VALUES (
        p_castle_id,
        p_pledge_id,
        p_type,
        p_world_id
    );
$function$;

COMMIT;
