-- Current Cached.exe ServerPacketEx handlers 21-24, 28 and 29.
--
-- Current machine code fixes every callable ABI and result-column order.
-- Modified 287, Classic 3.0 and Ertheia keep identical relational effects
-- for these six procedures. The PostgreSQL bodies below are translations of
-- that common donor behavior, not inferred packet-side state.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadmarketpricelist"()
RETURNS TABLE (
    item_type integer,
    enchant integer,
    avg_price double precision
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        market_price."item_type",
        market_price."enchant",
        market_price."avg_price"
    FROM "world"."item_market_price" AS market_price
    WHERE market_price."frequency" >= 10;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getallnobless"()
RETURNS TABLE (
    char_id integer,
    nobless_type integer,
    hero_type integer,
    win_count integer,
    previous_point integer,
    olympiad_point integer,
    match_count integer,
    olympiad_win_count integer,
    olympiad_lose_count integer,
    trade_point integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        nobless."char_id",
        nobless."nobless_type"::integer,
        nobless."hero_type"::integer,
        COALESCE(nobless."win_count", 0),
        COALESCE(nobless."previous_point", 0),
        COALESCE(nobless."olympiad_point", 0),
        COALESCE(nobless."match_count", 0),
        COALESCE(nobless."olympiad_win_count", 0),
        COALESCE(nobless."olympiad_lose_count", 0),
        nobless."trade_point"
    FROM "world"."user_nobless" AS nobless;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getnoblesstop10"(
    p_class integer,
    p_season integer
) RETURNS TABLE (
    char_id integer,
    char_name text,
    point integer,
    match_count integer,
    olympiad_win_count integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        user_row."char_id",
        user_row."char_name"::text,
        result_row."point",
        result_row."match_count",
        result_row."olympiad_win_count"
    FROM "world"."user_data" AS user_row
    JOIN "world"."olympiad_result" AS result_row
      ON result_row."char_id" = user_row."char_id"
    WHERE result_row."class" = p_class
      AND result_row."season" = p_season
      AND user_row."account_id" > -1
    -- Portable policy: the final character key uses ICU ko-u-ks-level2.
    -- This approximates, but does not exactly reproduce, the
    -- legacy SQL Server Wansung-v80 order/equality groups used by WITH TIES.
    ORDER BY
        result_row."point" DESC,
        result_row."olympiad_win_count" DESC,
        result_row."match_count" ASC,
        (user_row."char_name"::text COLLATE
            "world"."Korean_Wansung_CI_AS") ASC
    FETCH FIRST 10 ROWS WITH TIES;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getnoblessachievements"(
    p_char_id integer
) RETURNS TABLE (
    win_type integer,
    target integer,
    win_time integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        achievement."win_type",
        achievement."target",
        achievement."win_time"
    FROM "world"."nobless_achievements" AS achievement
    WHERE achievement."char_id" = p_char_id
    ORDER BY achievement."win_time" DESC;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_renewsubjob"(
    p_char_id integer,
    p_subjob_id integer,
    p_subjob_type integer,
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
    v_dual_class_id integer := -1;
    v_saved_sp bigint := p_sp;
    v_saved_henna_1 integer := p_henna_1;
    v_saved_henna_2 integer := p_henna_2;
    v_saved_henna_3 integer := p_henna_3;
    v_affected_rows integer := 0;
    v_char_name text;
    v_account_name text;
BEGIN
    BEGIN
        PERFORM 1
        FROM "world"."user_data" AS user_row
        WHERE user_row."char_id" = p_char_id;
        IF NOT FOUND THEN
            RAISE EXCEPTION USING ERRCODE = 'P0001';
        END IF;

        IF p_subjob_type = 1 THEN
            IF p_subjob_id <> p_old_subjob_id THEN
                RAISE EXCEPTION USING ERRCODE = 'P0001';
            END IF;
            v_dual_class_id := p_subjob_id;
        ELSE
            IF p_subjob_id <> p_old_subjob_id THEN
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
                    RAISE EXCEPTION USING ERRCODE = 'P0001';
                END IF;
            END IF;

            v_saved_sp := 0;
            v_saved_henna_1 := 0;
            v_saved_henna_2 := 0;
            v_saved_henna_3 := 0;
        END IF;

        IF v_dual_class_id = -1 THEN
            DELETE FROM "world"."shortcut_data"
            WHERE "char_id" = p_char_id
              AND "subjob_id" = p_subjob_id;

            DELETE FROM "world"."user_skill"
            WHERE "char_id" = p_char_id
              AND "subjob_id" = p_subjob_id;

            DELETE FROM "world"."user_skill_reuse_delay"
            WHERE "char_id" = p_char_id
              AND "subjob_id" = p_subjob_id;
        END IF;

        IF p_subjob_id NOT BETWEEN 1 AND 3 THEN
            RAISE EXCEPTION USING ERRCODE = 'P0001';
        END IF;

        UPDATE "world"."user_data" AS user_row
        SET "class" = p_new_class,
            "subjob_id" = p_subjob_id,
            "subjob1_class" = CASE
                WHEN p_subjob_id = 1 THEN p_new_class
                ELSE user_row."subjob1_class"
            END,
            "subjob2_class" = CASE
                WHEN p_subjob_id = 2 THEN p_new_class
                ELSE user_row."subjob2_class"
            END,
            "subjob3_class" = CASE
                WHEN p_subjob_id = 3 THEN p_new_class
                ELSE user_row."subjob3_class"
            END,
            "dual_class_id" = v_dual_class_id,
            "sp" = v_saved_sp,
            "exp" = p_new_exp,
            "lev" = p_new_level
        WHERE user_row."char_id" = p_char_id;
        GET DIAGNOSTICS v_affected_rows = ROW_COUNT;
        IF v_affected_rows <> 1 THEN
            RAISE EXCEPTION USING ERRCODE = 'P0001';
        END IF;

        UPDATE "world"."user_subjob" AS renewed_subjob
        SET "hp" = 1.0,
            "mp" = 1.0,
            "sp" = v_saved_sp,
            "exp" = p_new_exp,
            "level" = p_new_level,
            "henna_1" = v_saved_henna_1,
            "henna_2" = v_saved_henna_2,
            "henna_3" = v_saved_henna_3,
            "create_date" = "world"."mssql_getdate"(),
            "max_level" = p_new_level
        WHERE renewed_subjob."char_id" = p_char_id
          AND renewed_subjob."subjob_id" = p_subjob_id;

        DELETE FROM "world"."user_newactiveskill"
        WHERE "char_id" = p_char_id;

        UPDATE "world"."user_henna"
        SET "henna_1" = v_saved_henna_1,
            "henna_2" = v_saved_henna_2,
            "henna_3" = v_saved_henna_3
        WHERE "char_id" = p_char_id;
    EXCEPTION
        WHEN OTHERS THEN
            RETURN QUERY SELECT 0;
            RETURN;
    END;

    -- The SQL Server donor commits the renewal before writing action 4. A
    -- nested exception block gives the PostgreSQL function the same behavior:
    -- history failure does not turn a successful renewal into result zero.
    IF p_subjob_type = 1 THEN
        BEGIN
            SELECT user_row."char_name", user_row."account_name"
            INTO v_char_name, v_account_name
            FROM "world"."user_data" AS user_row
            WHERE user_row."char_id" = p_char_id;

            INSERT INTO "world"."user_history" (
                "char_name",
                "char_id",
                "log_action",
                "account_name",
                "create_date"
            ) VALUES (
                v_char_name,
                p_char_id,
                4,
                v_account_name,
                "world"."mssql_getdate"()
            );
        EXCEPTION
            WHEN OTHERS THEN
                NULL;
        END;
    END IF;

    RETURN QUERY SELECT p_subjob_id;
END;
$function$;

COMMIT;
