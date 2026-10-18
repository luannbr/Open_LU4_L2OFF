-- Current Cached.exe primary opcode 75 (RequestLoadBattleCamp)
-- persistence.
--
-- The current handler fixes one- and two-input query paths with the same
-- eight result columns. High Five, modified-287, Classic 3.0 and Ertheia
-- provide unanimous compatible substantive bodies.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_getresidencebyidtype"(
    p_residence_id integer,
    p_type integer
) RETURNS TABLE (
    battle_camp_id integer,
    owner_id integer,
    residence_id integer,
    max_hp integer,
    hp integer,
    x_pos integer,
    y_pos integer,
    z_pos integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        battle_camp."id",
        battle_camp."owner_id",
        battle_camp."residence_id",
        battle_camp."max_hp",
        battle_camp."hp",
        battle_camp."x_pos",
        battle_camp."y_pos",
        battle_camp."z_pos"
    FROM "world"."object_data" AS battle_camp
    WHERE battle_camp."residence_id" = p_residence_id
      AND battle_camp."type" = p_type;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getresidencebytype"(
    p_type integer
) RETURNS TABLE (
    battle_camp_id integer,
    owner_id integer,
    residence_id integer,
    max_hp integer,
    hp integer,
    x_pos integer,
    y_pos integer,
    z_pos integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        battle_camp."id",
        battle_camp."owner_id",
        battle_camp."residence_id",
        battle_camp."max_hp",
        battle_camp."hp",
        battle_camp."x_pos",
        battle_camp."y_pos",
        battle_camp."z_pos"
    FROM "world"."object_data" AS battle_camp
    WHERE battle_camp."type" = p_type;
$function$;

COMMIT;
