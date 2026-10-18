-- Current Cached.exe primary opcode 39 (RequestDestroyItem) pet lookup.
-- CPetDB::GetPet fixes one integer input and all thirteen result types.  The
-- ordered projection below is identical in the compatible 287, Classic 3.0
-- and Ertheia SQL exports. Item destruction itself persists through the
-- already-recovered CItem transaction path and needs no new procedure.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadpet"(
    p_pet_id integer
) RETURNS TABLE (
    npc_class_id integer,
    expoint bigint,
    hp double precision,
    mp double precision,
    sp bigint,
    meal integer,
    nick_name text,
    slot1 integer,
    slot2 integer,
    slot3 integer,
    down_exp bigint,
    was_dead integer,
    death_count integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        pet."npc_class_id",
        pet."expoint"::bigint,
        pet."hp"::double precision,
        pet."mp"::double precision,
        pet."sp"::bigint,
        pet."meal",
        pet."nick_name"::text,
        pet."slot1",
        pet."slot2",
        pet."slot3",
        pet."down_exp"::bigint,
        pet."was_dead"::integer,
        pet."death_count"
    FROM "world"."pet_data" AS pet
    WHERE pet."pet_id" = p_pet_id
$function$;

COMMIT;
