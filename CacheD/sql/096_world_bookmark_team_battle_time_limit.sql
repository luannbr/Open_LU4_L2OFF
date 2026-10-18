-- Current Cached.exe primary opcodes 178 and 180-184, 186-187.
--
-- The current image fixes the callable names, arities and result-column use.
-- Classic 3.0, modified 287 and Ertheia contain matching relational bodies
-- for bookmark management, TeamBattle load/register/unregister operations and
-- time-limit changes.
--
-- No inspected database export contains the bodies of lin_FinishTeamBattle,
-- lin_StartBattleRoyal or lin_StartFinal. Their three-input ABI is exact, but
-- the implementations below keep only the unanimously known
-- TeamBattle status update. They must not be cited as recovered original SQL.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

-- MSSQL lin_ManBookMark returns five columns for option 2 and four differently
-- typed columns for option 3. PostgreSQL functions have a fixed row type, so
-- this compatibility surface transports five text values. DBConn applies the
-- target output binding type to each fetched field just as ODBC did.
CREATE OR REPLACE FUNCTION "world"."lin_manbookmark"(
    p_option integer,
    p_char_id integer,
    p_bookmark_id integer,
    p_bookmark_name text,
    p_x integer,
    p_y integer,
    p_z integer
) RETURNS TABLE (
    value_1 text,
    value_2 text,
    value_3 text,
    value_4 text,
    value_5 text
)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF p_option = 1 THEN
        IF (
            SELECT count(*)
            FROM "world"."bookmark" AS bookmark
            WHERE bookmark."char_id" = p_char_id
        ) < 200 THEN
            INSERT INTO "world"."bookmark" (
                "char_id",
                "name",
                "world",
                "x",
                "y",
                "z"
            ) VALUES (
                p_char_id,
                p_bookmark_name,
                0,
                p_x,
                p_y,
                p_z
            );
        END IF;
        RETURN;
    END IF;

    IF p_option = 2 THEN
        RETURN QUERY
        SELECT
            bookmark."bookmarkid"::text,
            bookmark."name"::text,
            bookmark."x"::text,
            bookmark."y"::text,
            bookmark."z"::text
        FROM "world"."bookmark" AS bookmark
        WHERE bookmark."char_id" = p_char_id
        -- Portable policy: use PostgreSQL ICU ko-u-ks-level2 through the
        -- recovered collation name. This is an approximation.
        -- ICU does not reproduce the exact legacy SQL Server Wansung-v80
        -- ordering weights or its nvarchar trailing-padding behavior.
        ORDER BY
            (bookmark."name"::text COLLATE
                "world"."Korean_Wansung_CI_AS") ASC;
        RETURN;
    END IF;

    IF p_option = 3 THEN
        RETURN QUERY
        SELECT
            bookmark."name"::text,
            bookmark."x"::text,
            bookmark."y"::text,
            bookmark."z"::text,
            NULL::text
        FROM "world"."bookmark" AS bookmark
        WHERE bookmark."bookmarkid" = p_bookmark_id;
        RETURN;
    END IF;

    IF p_option = 4 THEN
        DELETE FROM "world"."bookmark"
        WHERE "bookmark"."bookmarkid" = p_bookmark_id;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadteambattleagitstatus"(
    p_agit_id integer
) RETURNS TABLE (team_battle_status integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT COALESCE(agit."team_battle_status", 0)::integer
    FROM "world"."agit" AS agit
    WHERE agit."id" = p_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadteambattleagitpledge"(
    p_agit_id integer
) RETURNS TABLE (
    pledge_id integer,
    propose_time integer,
    color integer,
    npc_type integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        pledge."pledge_id",
        pledge."propose_time",
        pledge."color",
        pledge."npc_type"
    FROM "world"."team_battle_agit_pledge" AS pledge
    WHERE pledge."agit_id" = p_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadteambattleagitmember"(
    p_agit_id integer
) RETURNS TABLE (
    char_id integer,
    pledge_id integer,
    propose_time integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        member."char_id",
        member."pledge_id",
        member."propose_time"
    FROM "world"."team_battle_agit_member" AS member
    WHERE member."agit_id" = p_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_newteambattleagitpledge"(
    p_agit_id integer,
    p_pledge_id integer,
    p_char_id integer,
    p_propose_time integer,
    p_color integer,
    p_npc_type integer
) RETURNS TABLE (result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    BEGIN
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
        result := 1;
    EXCEPTION WHEN OTHERS THEN
        result := 0;
    END;
    RETURN NEXT;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_newteambattleagitmember"(
    p_agit_id integer,
    p_char_id integer,
    p_pledge_id integer,
    p_propose_time integer
) RETURNS TABLE (result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    BEGIN
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
        result := 1;
    EXCEPTION WHEN OTHERS THEN
        result := 0;
    END;
    RETURN NEXT;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_unregisterteambattleagitpledge"(
    p_agit_id integer,
    p_pledge_id integer
) RETURNS TABLE (result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    BEGIN
        DELETE FROM "world"."team_battle_agit_pledge"
        WHERE "team_battle_agit_pledge"."agit_id" = p_agit_id
          AND "team_battle_agit_pledge"."pledge_id" = p_pledge_id;

        DELETE FROM "world"."team_battle_agit_member"
        WHERE "team_battle_agit_member"."agit_id" = p_agit_id
          AND "team_battle_agit_member"."pledge_id" = p_pledge_id;
        result := 1;
    EXCEPTION WHEN OTHERS THEN
        result := 0;
    END;
    RETURN NEXT;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_unregisterteambattleagitmember"(
    p_agit_id integer,
    p_char_id integer,
    p_pledge_id integer
) RETURNS TABLE (result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    BEGIN
        DELETE FROM "world"."team_battle_agit_member"
        WHERE "team_battle_agit_member"."agit_id" = p_agit_id
          AND "team_battle_agit_member"."char_id" = p_char_id
          AND "team_battle_agit_member"."pledge_id" = p_pledge_id;
        result := 1;
    EXCEPTION WHEN OTHERS THEN
        result := 0;
    END;
    RETURN NEXT;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_changetimedata"(
    p_char_name text,
    p_type integer,
    p_change_minutes integer
) RETURNS TABLE (
    account_id integer,
    used_sec integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_account_id integer := 0;
    v_used_sec integer := -1;
    v_change_seconds integer;
BEGIN
    v_change_seconds := p_change_minutes * 60;
    IF p_type < 1 OR p_type > 2 OR v_change_seconds <= 0 THEN
        RAISE EXCEPTION
            'Not valid parameter : charnname[%] type[%], min[%]',
            p_char_name,
            p_type,
            v_change_seconds;
    END IF;

    SELECT character."account_id"
    INTO v_account_id
    FROM "world"."user_data" AS character
    WHERE "world"."mssql_text_eq"(character."char_name", p_char_name)
    LIMIT 1;
    IF COALESCE(v_account_id, 0) <= 0 THEN
        RAISE EXCEPTION
            'Not valid account id : charnname[%] type[%], min[%]',
            p_char_name,
            p_type,
            v_change_seconds;
    END IF;

    SELECT time_data."used_sec"
    INTO v_used_sec
    FROM "world"."time_data" AS time_data
    WHERE time_data."account_id" = v_account_id
    LIMIT 1;
    IF COALESCE(v_used_sec, -1) < 0 THEN
        RAISE EXCEPTION
            'Not exist time data : account_id[%], charnname[%], type[%], min[%]',
            v_account_id,
            p_char_name,
            p_type,
            v_change_seconds;
    END IF;

    IF p_type = 1 THEN
        v_used_sec := v_used_sec + v_change_seconds;
    ELSE
        v_used_sec := v_used_sec - LEAST(v_used_sec, v_change_seconds);
    END IF;

    UPDATE "world"."time_data"
    SET "used_sec" = v_used_sec
    WHERE "time_data"."account_id" = v_account_id;

    account_id := v_account_id;
    used_sec := v_used_sec;
    RETURN NEXT;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_finishteambattle"(
    p_agit_id integer,
    p_status integer,
    p_event_value integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."agit"
    SET "team_battle_status" = p_status
    WHERE "agit"."id" = p_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_startbattleroyal"(
    p_agit_id integer,
    p_status integer,
    p_event_value integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."agit"
    SET "team_battle_status" = p_status
    WHERE "agit"."id" = p_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_startfinal"(
    p_agit_id integer,
    p_status integer,
    p_event_value integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."agit"
    SET "team_battle_status" = p_status
    WHERE "agit"."id" = p_agit_id;
$function$;

COMMENT ON FUNCTION "world"."lin_finishteambattle"(
    integer,
    integer,
    integer
) IS 'ABI-constrained fallback: original external SQL body is unavailable; p_event_value is intentionally unused.';

COMMENT ON FUNCTION "world"."lin_startbattleroyal"(
    integer,
    integer,
    integer
) IS 'ABI-constrained fallback: original external SQL body is unavailable; p_event_value is intentionally unused.';

COMMENT ON FUNCTION "world"."lin_startfinal"(
    integer,
    integer,
    integer
) IS 'ABI-constrained fallback: original external SQL body is unavailable; p_event_value is intentionally unused.';

COMMIT;
