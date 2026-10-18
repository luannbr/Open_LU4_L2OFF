-- Current Cached.exe primary opcodes 111..115 alliance mutations.
--
-- CAllianceDb 0x0044df7c..0x0044f6d9 fixes the five callable signatures,
-- bind order and one-integer result rows. Classic 3.0, modified protocol-287
-- and Ertheia SQL donors agree on the relational bodies. The only transport
-- change here is MSSQL stored procedures to PostgreSQL functions.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_createalliance"(
    p_name text,
    p_master_pledge_id integer,
    p_world_id integer
) RETURNS TABLE (result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_alliance_id integer := 0;
    v_updated_rows integer := 0;
BEGIN
    -- Shipping MSSQL comparisons use the database's case-insensitive
    -- collation. Preserve that behavior on PostgreSQL.
    IF p_name LIKE ' ' THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END IF;

    IF EXISTS (
        SELECT 1
        FROM "world"."user_prohibit" AS prohibited
        WHERE "world"."mssql_text_eq"(prohibited."char_name", p_name)
    ) THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END IF;

    IF EXISTS (
        SELECT 1
        FROM "world"."user_prohibit_word" AS prohibited
        WHERE position(lower(prohibited."words") IN lower(p_name)) > 0
    ) THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END IF;

    -- The target's name primary key inherits the same case-insensitive
    -- collation. Migration 186 gives the PostgreSQL key those comparison
    -- flags, so the key itself resolves concurrent duplicates without a
    -- stronger portable table lock.
    IF EXISTS (
        SELECT 1
        FROM "world"."alliance" AS existing
        WHERE "world"."mssql_text_eq"(existing."name", p_name)
    ) THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END IF;

    BEGIN
        INSERT INTO "world"."alliance" AS created (
            "name",
            "master_pledge_id",
            "world_id"
        ) VALUES (
            p_name,
            p_master_pledge_id,
            p_world_id
        )
        RETURNING created."id"
        INTO v_alliance_id;

        UPDATE "world"."pledge" AS master
        SET "alliance_id" = v_alliance_id
        WHERE master."pledge_id" = p_master_pledge_id;
        GET DIAGNOSTICS v_updated_rows = ROW_COUNT;
        IF v_updated_rows <> 1 THEN
            RAISE EXCEPTION USING
                ERRCODE = 'P0001',
                MESSAGE = 'lin_CreateAlliance master update count differs';
        END IF;
    EXCEPTION
        WHEN raise_exception OR unique_violation OR
             string_data_right_truncation OR check_violation OR
             not_null_violation THEN
            RETURN QUERY SELECT 0;
            RETURN;
    END;

    RETURN QUERY SELECT v_alliance_id;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_joinalliance"(
    p_alliance_id integer,
    p_member_pledge_id integer
) RETURNS TABLE (result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_updated_rows integer := 0;
BEGIN
    UPDATE "world"."pledge" AS member
    SET "alliance_id" = p_alliance_id
    WHERE member."pledge_id" = p_member_pledge_id;
    GET DIAGNOSTICS v_updated_rows = ROW_COUNT;
    RETURN QUERY SELECT CASE WHEN v_updated_rows = 1 THEN 1 ELSE 0 END;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_withdrawalliance"(
    p_alliance_id integer,
    p_member_pledge_id integer,
    p_alliance_withdraw_time integer
) RETURNS TABLE (result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_updated_rows integer := 0;
BEGIN
    -- p_alliance_id is not part of the donor UPDATE condition.
    -- It remains in the current callable ABI even though the SQL body ignores
    -- it, just as the shipping procedure does.
    UPDATE "world"."pledge" AS member
    SET "alliance_id" = 0,
        "alliance_withdraw_time" = p_alliance_withdraw_time
    WHERE member."pledge_id" = p_member_pledge_id;
    GET DIAGNOSTICS v_updated_rows = ROW_COUNT;
    RETURN QUERY SELECT CASE WHEN v_updated_rows = 1 THEN 1 ELSE 0 END;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_oustmemberpledge"(
    p_alliance_id integer,
    p_member_pledge_id integer,
    p_oust_time integer
) RETURNS TABLE (result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_updated_rows integer := 0;
BEGIN
    BEGIN
        UPDATE "world"."alliance" AS target
        SET "oust_time" = p_oust_time
        WHERE target."id" = p_alliance_id;
        GET DIAGNOSTICS v_updated_rows = ROW_COUNT;
        IF v_updated_rows <> 1 THEN
            RAISE EXCEPTION USING
                ERRCODE = 'P0001',
                MESSAGE = 'lin_OustMemberPledge alliance update count differs';
        END IF;

        UPDATE "world"."pledge" AS member
        SET "alliance_id" = 0,
            "alliance_ousted_time" = p_oust_time
        WHERE member."pledge_id" = p_member_pledge_id;
        GET DIAGNOSTICS v_updated_rows = ROW_COUNT;
        IF v_updated_rows <> 1 THEN
            RAISE EXCEPTION USING
                ERRCODE = 'P0001',
                MESSAGE = 'lin_OustMemberPledge member update count differs';
        END IF;
    EXCEPTION
        WHEN raise_exception THEN
            RETURN QUERY SELECT 0;
            RETURN;
    END;

    RETURN QUERY SELECT 1;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_dismissalliance"(
    p_alliance_id integer,
    p_master_pledge_id integer,
    p_dismiss_time integer
) RETURNS TABLE (result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_changed_rows integer := 0;
BEGIN
    BEGIN
        DELETE FROM "world"."alliance" AS target
        WHERE target."id" = p_alliance_id;
        GET DIAGNOSTICS v_changed_rows = ROW_COUNT;
        IF v_changed_rows <> 1 THEN
            RAISE EXCEPTION USING
                ERRCODE = 'P0001',
                MESSAGE = 'lin_DismissAlliance delete count differs';
        END IF;

        UPDATE "world"."pledge" AS master
        SET "alliance_dismiss_time" = p_dismiss_time
        WHERE master."pledge_id" = p_master_pledge_id;
        GET DIAGNOSTICS v_changed_rows = ROW_COUNT;
        IF v_changed_rows <> 1 THEN
            RAISE EXCEPTION USING
                ERRCODE = 'P0001',
                MESSAGE = 'lin_DismissAlliance master update count differs';
        END IF;
    EXCEPTION
        WHEN raise_exception THEN
            RETURN QUERY SELECT 0;
            RETURN;
    END;

    -- The MSSQL donor performs this immediately after committing the first
    -- two mutations. PostgreSQL keeps the complete function atomic while
    -- preserving the same successful relational state.
    UPDATE "world"."pledge" AS member
    SET "alliance_id" = 0
    WHERE member."alliance_id" = p_alliance_id;

    RETURN QUERY SELECT 1;
END;
$function$;

COMMIT;
