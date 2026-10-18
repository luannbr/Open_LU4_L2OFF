-- Current Cached.exe primary opcode 63 (RequestCreatePledgeByNpc)
-- persistence.
--
-- CPledgeDb::CreatePledge 0x00519790 fixes the ordered ABI as
-- (name UTF-16, ruler_id integer, world_id integer) with the result
-- (pledge_id integer, pledge_ruler_name UTF-16).  High Five, modified-287,
-- Classic 3.0 and Ertheia provide identical full-arity procedure bodies.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_createpledge"(
    p_name text,
    p_ruler_id integer,
    p_world_id integer
) RETURNS TABLE (
    pledge_id integer,
    pledge_ruler_name text
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_pledge_id integer := 0;
    v_ruler_name text := '';
    v_updated_rows integer := 0;
BEGIN
    -- These comparisons are explicit because the shipping MSSQL database
    -- uses case-insensitive character comparison while PostgreSQL does not.
    IF p_name LIKE ' ' THEN
        RETURN QUERY SELECT 0, ''::text;
        RETURN;
    END IF;

    IF EXISTS (
        SELECT 1
        FROM "world"."user_prohibit" AS prohibited
        WHERE "world"."mssql_text_eq"(prohibited."char_name", p_name)
    ) THEN
        RETURN QUERY SELECT 0, ''::text;
        RETURN;
    END IF;

    IF EXISTS (
        SELECT 1
        FROM "world"."user_prohibit_word" AS prohibited
        WHERE position(lower(prohibited."words") IN lower(p_name)) > 0
    ) THEN
        RETURN QUERY SELECT 0, ''::text;
        RETURN;
    END IF;

    IF EXISTS (
        SELECT 1
        FROM "world"."changed_name_by_merge" AS renamed
        WHERE renamed."type" = 1
          AND "world"."mssql_text_eq"(renamed."name_new", p_name)
    ) THEN
        RETURN QUERY SELECT 0, ''::text;
        RETURN;
    END IF;

    -- The target relies on its case-insensitive unique pledge-name index.
    -- Migration 186 applies the matching comparison flags to uq_pledge_1,
    -- so a concurrent duplicate is rejected by the key just as it is on SQL
    -- Server. No stronger portable table lock belongs here.
    IF EXISTS (
        SELECT 1
        FROM "world"."pledge" AS existing
        WHERE "world"."mssql_text_eq"(existing."name", p_name)
    ) THEN
        RETURN QUERY SELECT 0, ''::text;
        RETURN;
    END IF;

    BEGIN
        -- The two current-only NOT NULL fields are zero in the five-argument
        -- CPledge constructor.  The compatible donor procedure predates the
        -- dump's missing default constraints, so spell out those same values
        -- while retaining its three-column logical input contract.
        INSERT INTO "world"."pledge" AS created (
            "name",
            "ruler_id",
            "world_id",
            "new_emblem_id",
            "mastery_reset_time"
        ) VALUES (
            p_name,
            p_ruler_id,
            p_world_id,
            0,
            0
        )
        RETURNING created."pledge_id"
        INTO v_pledge_id;

        UPDATE "world"."user_data" AS ruler
        SET "pledge_id" = v_pledge_id
        WHERE ruler."char_id" = p_ruler_id;
        GET DIAGNOSTICS v_updated_rows = ROW_COUNT;
        IF v_updated_rows <> 1 THEN
            RAISE EXCEPTION USING
                ERRCODE = 'P0001',
                MESSAGE = 'lin_CreatePledge ruler update count differs';
        END IF;

        SELECT ruler."char_name"
        INTO v_ruler_name
        FROM "world"."user_data" AS ruler
        WHERE ruler."char_id" = p_ruler_id;
    EXCEPTION
        WHEN raise_exception OR unique_violation OR
             string_data_right_truncation OR check_violation OR
             not_null_violation THEN
            RETURN QUERY SELECT 0, ''::text;
            RETURN;
    END;

    RETURN QUERY SELECT v_pledge_id, v_ruler_name;
END;
$function$;

COMMIT;
