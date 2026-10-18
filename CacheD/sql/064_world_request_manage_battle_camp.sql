-- Current Cached.exe primary opcode 74 (RequestManageBattleCamp)
-- persistence.
--
-- CBattleCampDb 0x004512cc..0x0045179b fixes the three ordered procedure
-- ABIs and result shape. Modified-287, Classic 3.0 and Ertheia are the
-- compatible full-arity donors. Their substantive bodies are unanimous.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_installbattlecamp"(
    p_pledge_id integer,
    p_pledge_world_id integer,
    p_castle_id integer,
    p_max_hp integer,
    p_hp integer,
    p_x integer,
    p_y integer,
    p_z integer,
    p_type integer
) RETURNS TABLE (return_id integer)
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."object_data" AS battle_camp (
        "owner_id",
        "residence_id",
        "max_hp",
        "hp",
        "x_pos",
        "y_pos",
        "z_pos",
        "type",
        "world_id"
    ) VALUES (
        p_pledge_id,
        p_castle_id,
        p_max_hp,
        p_hp,
        p_x,
        p_y,
        p_z,
        p_type,
        p_pledge_world_id
    )
    RETURNING battle_camp."id";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_uninstallbattlecamp"(
    p_pledge_id integer,
    p_pledge_world_id integer,
    p_type integer
) RETURNS TABLE (return_id integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    -- MSSQL returns the selected IDs and then deletes every matching row.
    -- RETURN QUERY creates that same pre-delete result set.
    RETURN QUERY
    SELECT battle_camp."id"
    FROM "world"."object_data" AS battle_camp
    WHERE battle_camp."owner_id" = p_pledge_id
      AND battle_camp."type" = p_type
      AND battle_camp."world_id" = p_pledge_world_id;

    DELETE FROM "world"."object_data" AS battle_camp
    WHERE battle_camp."owner_id" = p_pledge_id
      AND battle_camp."type" = p_type
      AND battle_camp."world_id" = p_pledge_world_id;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_uninstallallbattlecamp"(
    p_castle_id integer,
    p_type integer
) RETURNS TABLE (return_id integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    RETURN QUERY
    SELECT battle_camp."id"
    FROM "world"."object_data" AS battle_camp
    WHERE battle_camp."residence_id" = p_castle_id
      AND battle_camp."type" = p_type;

    DELETE FROM "world"."object_data" AS battle_camp
    WHERE battle_camp."residence_id" = p_castle_id
      AND battle_camp."type" = p_type;
END;
$function$;

COMMIT;
