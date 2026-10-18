-- Current Cached.exe primary opcode 220 (RequestCreateSubJob) persistence.
--
-- CUser::CreateSubJob 0x005af9c8 fixes the complete 15-input ABI and one
-- integer result. Classic 3.0, modified 287 and Ertheia keep identical
-- full-arity procedure bodies. The nested block keeps their atomic
-- commit-on-nonzero-result behavior on PostgreSQL.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

-- These defaults are present on both non-null columns in the selected donor
-- DDL. Migration 019 kept the columns but omitted the separate ALTER
-- TABLE default constraints written later in the SQL Server script.
ALTER TABLE "world"."user_subjob"
    ALTER COLUMN "max_level" SET DEFAULT 0;
ALTER TABLE "world"."user_subjob"
    ALTER COLUMN "pp" SET DEFAULT 0;

-- Current helper 0x00433684 binds the four int32 vitality values with
-- SQL_C_SLONG/SQL_DOUBLE, while the adjacent helper 0x00433724 uses ordinary
-- SQL_INTEGER. Keep both machine-visible callable ABIs over one relation.
CREATE OR REPLACE FUNCTION "world"."lin_savecharvitality"(
    p_char_id integer,
    p_subjob_id integer,
    p_vital_point double precision,
    p_restore_count double precision,
    p_latest_reset_date_vp double precision,
    p_latest_reset_date_rc double precision
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    SELECT "world"."lin_savecharvitality"(
        p_char_id,
        p_subjob_id,
        p_vital_point::integer,
        p_restore_count::integer,
        p_latest_reset_date_vp::integer,
        p_latest_reset_date_rc::integer
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_createsubjob"(
    p_char_id integer,
    p_new_subjob_id integer,
    p_new_class integer,
    p_new_level integer,
    p_new_exp bigint,
    p_old_subjob_id integer,
    p_hp double precision,
    p_mp double precision,
    p_sp bigint,
    p_exp bigint,
    p_level integer,
    p_henna_1 integer,
    p_henna_2 integer,
    p_henna_3 integer,
    p_pp integer
) RETURNS TABLE (
    result integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_create_date timestamp without time zone;
    v_affected_rows integer := 0;
BEGIN
    -- The donor transaction first updates exactly one user_data row. Locking
    -- that row also serializes concurrent subjob changes for one character.
    SELECT user_row."create_date"
    INTO v_create_date
    FROM "world"."user_data" AS user_row
    WHERE user_row."char_id" = p_char_id
    FOR UPDATE;

    IF NOT FOUND OR p_new_subjob_id NOT BETWEEN 1 AND 3 THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END IF;

    -- Missing main-class state is created by the donor body below. A missing
    -- non-main old subjob makes its exact-one-row update fail and rolls the
    -- whole stored procedure back.
    IF p_old_subjob_id <> 0 AND NOT EXISTS (
        SELECT 1
        FROM "world"."user_subjob" AS old_subjob
        WHERE old_subjob."char_id" = p_char_id
          AND old_subjob."subjob_id" = p_old_subjob_id
    ) THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END IF;

    BEGIN
        UPDATE "world"."user_data" AS user_row
        SET "class" = p_new_class,
            "subjob_id" = p_new_subjob_id,
            "subjob1_class" = CASE
                WHEN p_new_subjob_id = 1 THEN p_new_class
                ELSE user_row."subjob1_class"
            END,
            "subjob2_class" = CASE
                WHEN p_new_subjob_id = 2 THEN p_new_class
                ELSE user_row."subjob2_class"
            END,
            "subjob3_class" = CASE
                WHEN p_new_subjob_id = 3 THEN p_new_class
                ELSE user_row."subjob3_class"
            END,
            "sp" = 0,
            "exp" = p_new_exp,
            "lev" = p_new_level,
            "pp" = 0
        WHERE user_row."char_id" = p_char_id;
        GET DIAGNOSTICS v_affected_rows = ROW_COUNT;
        IF v_affected_rows <> 1 THEN
            RAISE EXCEPTION USING
                ERRCODE = 'P0001',
                MESSAGE = 'lin_CreateSubJob user update count differs';
        END IF;

        IF p_old_subjob_id = 0 THEN
            INSERT INTO "world"."user_subjob" (
                "char_id",
                "subjob_id",
                "hp",
                "mp",
                "sp",
                "exp",
                "level",
                "henna_1",
                "henna_2",
                "henna_3",
                "create_date",
                "max_level",
                "pp"
            ) VALUES (
                p_char_id,
                0,
                p_hp,
                p_mp,
                p_sp,
                p_exp,
                p_level,
                p_henna_1,
                p_henna_2,
                p_henna_3,
                v_create_date,
                0,
                p_pp
            )
            ON CONFLICT ("char_id", "subjob_id") DO NOTHING;
        END IF;

        UPDATE "world"."user_subjob" AS old_subjob
        SET "hp" = p_hp,
            "mp" = p_mp,
            "sp" = p_sp,
            "exp" = p_exp,
            "level" = p_level,
            "henna_1" = p_henna_1,
            "henna_2" = p_henna_2,
            "henna_3" = p_henna_3,
            "pp" = p_pp
        WHERE old_subjob."char_id" = p_char_id
          AND old_subjob."subjob_id" = p_old_subjob_id;
        GET DIAGNOSTICS v_affected_rows = ROW_COUNT;
        IF v_affected_rows <> 1 THEN
            RAISE EXCEPTION USING
                ERRCODE = 'P0001',
                MESSAGE = 'lin_CreateSubJob old subjob update count differs';
        END IF;

        INSERT INTO "world"."user_subjob" (
            "char_id",
            "hp",
            "mp",
            "sp",
            "exp",
            "level",
            "henna_1",
            "henna_2",
            "henna_3",
            "subjob_id",
            "create_date",
            "max_level",
            "pp"
        ) VALUES (
            p_char_id,
            1.0,
            1.0,
            0,
            p_new_exp,
            p_new_level,
            0,
            0,
            0,
            p_new_subjob_id,
            "world"."mssql_getdate"(),
            0,
            0
        )
        ON CONFLICT ("char_id", "subjob_id") DO NOTHING;

        DELETE FROM "world"."user_newactiveskill" AS active_skill
        WHERE active_skill."char_id" = p_char_id;

        UPDATE "world"."user_henna" AS henna
        SET "henna_1" = 0,
            "henna_2" = 0,
            "henna_3" = 0
        WHERE henna."char_id" = p_char_id;
    EXCEPTION
        WHEN integrity_constraint_violation OR data_exception OR
             raise_exception THEN
            RETURN QUERY SELECT 0;
            RETURN;
    END;

    RETURN QUERY SELECT p_new_subjob_id;
END;
$function$;

COMMIT;
