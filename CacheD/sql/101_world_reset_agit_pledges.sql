-- Current Cached.exe primary opcodes 200-202.
--
-- The current machine code fixes the procedure names, parameter order, and
-- call arity.  Every substantive full-arity donor family has the same
-- normalized relational effect. Incompatible zero-parameter extender stubs
-- are excluded.  lin_RegisterSiegeAgitPledge is already migration 098.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_setteambattlenpctype"(
    p_agit_id integer,
    p_pledge_id integer,
    p_npc_type integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."team_battle_agit_pledge"
    SET "npc_type" = p_npc_type
    WHERE "team_battle_agit_pledge"."agit_id" = p_agit_id
      AND "team_battle_agit_pledge"."pledge_id" = p_pledge_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletesiegeagitpledge"(
    p_agit_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."siege_agit_pledge"
    WHERE "siege_agit_pledge"."agit_id" = p_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deleteteambattleagitpledge"(
    p_agit_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."team_battle_agit_pledge"
    WHERE "team_battle_agit_pledge"."agit_id" = p_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_insertintoteambattleagitpledge"(
    p_agit_id integer,
    p_pledge_id integer,
    p_propose_time integer,
    p_color integer,
    p_npc_type integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."team_battle_agit_pledge" (
        "agit_id",
        "pledge_id",
        "propose_time",
        "color",
        "npc_type"
    ) VALUES (
        p_agit_id,
        p_pledge_id,
        p_propose_time,
        p_color,
        p_npc_type
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deleteteambattleagitmember"(
    p_agit_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."team_battle_agit_member"
    WHERE "team_battle_agit_member"."agit_id" = p_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_insertintoteambattleagitmember"(
    p_agit_id integer,
    p_char_id integer,
    p_pledge_id integer,
    p_propose_time integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."team_battle_agit_member" (
        "agit_id",
        "char_id",
        "pledge_id",
        "propose_time"
    ) VALUES (
        p_agit_id,
        p_char_id,
        p_pledge_id,
        p_propose_time
    );
$function$;

COMMIT;
