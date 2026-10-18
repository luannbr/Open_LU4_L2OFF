-- Current Cached.exe primary opcodes 116..118 pet lifecycle persistence.
--
-- CPet/CWareHouse machine code fixes all four callable signatures and bind
-- order. Classic 3.0, modified protocol-287 and Ertheia donors agree on the
-- relational bodies. Only the MSSQL-to-PostgreSQL transport changes here.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_createpet"(
    p_pet_dbid integer,
    p_npc_class_id integer,
    p_experience bigint,
    p_hp double precision,
    p_mp double precision,
    p_meal integer,
    p_spell_point bigint
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."pet_data" (
        "pet_id",
        "npc_class_id",
        "expoint",
        "hp",
        "mp",
        "meal",
        "sp"
    ) VALUES (
        p_pet_dbid,
        p_npc_class_id,
        p_experience,
        p_hp,
        p_mp,
        p_meal,
        p_spell_point
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletepet"(
    p_pet_id integer,
    p_character_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_pet_class_id integer := 0;
BEGIN
    SELECT COALESCE((
        SELECT pet."npc_class_id"
        FROM "world"."pet_data" AS pet
        WHERE pet."pet_id" = p_pet_id
    ), 0)
    INTO v_pet_class_id;

    DELETE FROM "world"."pet_data" AS pet
    WHERE pet."pet_id" = p_pet_id;

    IF v_pet_class_id > 0 AND p_character_id > 0 THEN
        DELETE FROM "world"."user_summon_data" AS summon
        WHERE summon."summon_class_id" = v_pet_class_id
          AND summon."char_id" = p_character_id;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savepet"(
    p_pet_id integer,
    p_experience bigint,
    p_hp double precision,
    p_mp double precision,
    p_spell_point bigint,
    p_meal integer,
    p_nickname text,
    p_slot1 integer,
    p_slot2 integer,
    p_slot3 integer,
    p_down_experience bigint,
    p_was_dead integer,
    p_death_count integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."pet_data" AS pet
    SET "expoint" = p_experience,
        "hp" = p_hp,
        "mp" = p_mp,
        "sp" = p_spell_point,
        "meal" = p_meal,
        "nick_name" = p_nickname,
        "slot1" = p_slot1,
        "slot2" = p_slot2,
        "slot3" = p_slot3,
        "down_exp" = p_down_experience,
        "was_dead" = p_was_dead,
        "death_count" = p_death_count
    WHERE pet."pet_id" = p_pet_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadpetlist"(
    p_char_id integer
) RETURNS TABLE (
    pet_id integer,
    item_type integer,
    enchant integer,
    eroded integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        pet."pet_id"::integer,
        item."item_type"::integer,
        item."enchant"::integer,
        item."eroded"::integer
    FROM "world"."user_item" AS item
    INNER JOIN "world"."pet_data" AS pet
      ON pet."pet_id" = item."item_id"
    WHERE item."char_id" = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_checkpetname"(
    p_pet_name text
) RETURNS TABLE (result integer)
LANGUAGE plpgsql
STABLE
AS $function$
DECLARE
    v_pet_name text := rtrim(p_pet_name);
    v_result integer := 1;
BEGIN
    -- The donor mutates one result variable in this exact order. A later
    -- condition therefore overrides an earlier error code.
    -- Preserve the donor's post-RTRIM Unicode LIKE, including its significant
    -- one-space pattern, instead of turning it into an empty-string test.
    IF v_pet_name LIKE ' ' THEN
        v_result := -1;
    END IF;

    IF EXISTS (
        SELECT 1
        FROM "world"."user_prohibit" AS prohibited
        WHERE "world"."mssql_text_eq"(prohibited."char_name", v_pet_name)
    ) THEN
        v_result := -2;
    END IF;

    IF EXISTS (
        SELECT 1
        FROM "world"."user_prohibit_word" AS prohibited
        WHERE position(lower(prohibited."words") IN lower(v_pet_name)) > 0
    ) THEN
        v_result := -3;
    END IF;

    IF EXISTS (
        SELECT 1
        FROM "world"."pet_data" AS pet
        WHERE "world"."mssql_text_eq"(pet."nick_name", v_pet_name)
    ) THEN
        v_result := -4;
    END IF;

    RETURN QUERY SELECT v_result;
END;
$function$;

COMMIT;
