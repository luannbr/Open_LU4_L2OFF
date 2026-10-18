-- Current Cached.exe extended slots 391-400: World Chat, dual-class and
-- Premium Kamaroka persistence. Matching-point slots 395/396 reuse the
-- lin_LoadMatchingPoint/lin_SaveMatchingPoint surfaces already supplied by
-- migrations 019 and 114.
--
-- The current image fixes every callable ABI below. Classic 3.0, modified
-- 287, Classic 3.0 and Ertheia keep matching bodies for
-- lin_LoadWorldChat, lin_ModifyWorldChat, lin_ResetWorldChat,
-- lin_CreateDualJob and all four Premium Kamaroka procedures. No supplied
-- database archive keeps lin_IncWorldChat. Its
-- body is therefore limited to the current two-input ABI and the paired
-- L2Server's load-then-increment behavior: add the requested amount to an
-- existing character row, without inventing an insert path.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadworldchat"(
    p_char_id integer,
    p_max_count integer
) RETURNS TABLE (
    point integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    RETURN QUERY
        SELECT world_chat."point"
        FROM "world"."user_worldchat" AS world_chat
        WHERE world_chat."char_id" = p_char_id;
    IF FOUND THEN
        RETURN;
    END IF;

    IF p_max_count <> -1 THEN
        INSERT INTO "world"."user_worldchat" (
            "char_id",
            "point"
        ) VALUES (
            p_char_id,
            p_max_count
        );
        RETURN QUERY SELECT p_max_count;
    ELSE
        RETURN QUERY SELECT -1;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_incworldchat"(
    p_char_id integer,
    p_increment integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_worldchat" AS world_chat
    SET "point" = world_chat."point" + p_increment
    WHERE world_chat."char_id" = p_char_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_modifyworldchat"(
    p_char_id integer,
    p_point integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."user_worldchat" AS world_chat
    SET "point" = p_point
    WHERE world_chat."char_id" = p_char_id;

    IF NOT FOUND THEN
        INSERT INTO "world"."user_worldchat" (
            "char_id",
            "point"
        ) VALUES (
            p_char_id,
            p_point
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_resetworldchat"(
    p_next_reset_time integer,
    p_max_count integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."user_worldchat"
    SET "point" = p_max_count;

    UPDATE "world"."user_worldchat" AS world_chat
    SET "point" = p_next_reset_time
    WHERE world_chat."char_id" = -1;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_createdualjob"(
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
    -- The paired current sender always requests dual slot 1, and the three
    -- compatible procedure bodies keep only that active branch.
    IF p_new_subjob_id <> 1 THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END IF;

    SELECT user_row."create_date"
    INTO v_create_date
    FROM "world"."user_data" AS user_row
    WHERE user_row."char_id" = p_char_id
    FOR UPDATE;

    IF NOT FOUND THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END IF;
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
            "subjob1_class" = p_new_class,
            "dual_class_id" = p_new_subjob_id,
            "sp" = 0,
            "exp" = p_new_exp,
            "lev" = p_new_level,
            "pp" = 0
        WHERE user_row."char_id" = p_char_id;
        GET DIAGNOSTICS v_affected_rows = ROW_COUNT;
        IF v_affected_rows <> 1 THEN
            RAISE EXCEPTION USING
                ERRCODE = 'P0001',
                MESSAGE = 'lin_CreateDualJob user update count differs';
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
                MESSAGE = 'lin_CreateDualJob old subjob update count differs';
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

CREATE OR REPLACE FUNCTION "world"."lin_loadpremiumkamaround"()
RETURNS TABLE (
    round_no integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT COALESCE(MAX(record."round_no"), 1)
    FROM "world"."premium_kamaloka_record" AS record
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadpremiumkamarank"(
    p_round_no integer,
    p_maximum_rank integer
) RETURNS TABLE (
    rank_number integer,
    party_no integer,
    score integer,
    character_list varchar(128)
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        ranked.rank_number,
        ranked.party_no,
        ranked.score,
        ranked.character_list
    FROM (
        SELECT
            rank() OVER (
                ORDER BY record."score" DESC
            )::integer AS rank_number,
            record."party_no",
            record."score",
            record."char_list" AS character_list
        FROM "world"."premium_kamaloka_record" AS record
        WHERE record."round_no" = p_round_no
          AND record."score" > 0
    ) AS ranked
    WHERE ranked.rank_number <= p_maximum_rank
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savepremiumkamapartyrecord"(
    p_round_no integer,
    p_score integer,
    p_character_list varchar(128)
) RETURNS TABLE (
    party_no integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_party_no integer;
BEGIN
    INSERT INTO "world"."premium_kamaloka_record" AS inserted (
        "round_no",
        "score",
        "char_list",
        "saved_time"
    ) VALUES (
        p_round_no,
        p_score,
        p_character_list,
        "world"."mssql_getdate"()
    )
    RETURNING inserted."party_no"
    INTO v_party_no;

    IF p_score = 0 THEN
        WITH ranked AS (
            SELECT
                record."round_no",
                record."party_no",
                rank() OVER (
                    ORDER BY record."score" DESC
                )::integer AS rank_number
            FROM "world"."premium_kamaloka_record" AS record
            WHERE record."round_no" = p_round_no - 1
        )
        UPDATE "world"."premium_kamaloka_record" AS record
        SET "ranking" = ranked.rank_number
        FROM ranked
        WHERE record."round_no" = ranked.round_no
          AND record."party_no" = ranked.party_no;
    END IF;

    RETURN QUERY SELECT v_party_no;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savepremiumkamamemberrecord"(
    p_round_no integer,
    p_party_no integer,
    p_char_id integer,
    p_score integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."premium_kamaloka_mybest" (
        "round_no",
        "party_no",
        "char_id",
        "score",
        "check_flag"
    ) VALUES (
        p_round_no,
        p_party_no,
        p_char_id,
        p_score,
        0
    )
    ON CONFLICT ("round_no", "char_id") DO UPDATE
    SET "party_no" = EXCLUDED."party_no",
        "score" = EXCLUDED."score"
    WHERE EXCLUDED."score" >
          "premium_kamaloka_mybest"."score"
$function$;

COMMIT;
