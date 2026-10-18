-- Current Cached.exe extended slots 401-410: personal Premium Kamaroka
-- ranking, UI counters, client INI, DroidNPC appearance, Honors skills and
-- wedding persistence. Slot 405 has no SQL surface of its own. It reads the
-- already recovered account, character, inventory, pledge and alliance
-- owners. The wedding load surface reached by slot 411 is supplied here so
-- that the start/end state introduced by slot 410 is readable.
--
-- The current image fixes every callable ABI below. Classic 3.0, modified
-- 287 and Ertheia agree on the relational bodies of
-- lin_LoadPremiumKamaBestScore, lin_UpdateUICount, lin_LoadClientIni and
-- lin_SaveClientIni. No supplied compatible database archive keeps the
-- Honors or wedding procedure bodies. Their two small relations and CRUD
-- functions are therefore an portable, current-ABI-constrained
-- persistence model: it exposes only state observed by the current CacheD
-- methods and does not add history, timestamps or inferred game rules.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadpremiumkamabestscore"(
    p_round_no integer,
    p_character_id integer
) RETURNS TABLE (
    score integer,
    character_list varchar(128),
    party_no integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT best."score",
           record."char_list" AS character_list,
           best."party_no"
    FROM "world"."premium_kamaloka_mybest" AS best
    JOIN "world"."premium_kamaloka_record" AS record
      ON record."party_no" = best."party_no"
    WHERE best."round_no" = p_round_no
      AND best."char_id" = p_character_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updateuicount"(
    p_ui_id integer,
    p_ui_count integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_log_date integer :=
        to_char("world"."mssql_getdate"()::date, 'YYYYMMDD')::integer;
BEGIN
    UPDATE "world"."ui_event_count" AS event_count
    SET "ui_count" = COALESCE(event_count."ui_count", 0) + p_ui_count
    WHERE event_count."log_date" = v_log_date
      AND event_count."ui_id" = p_ui_id;

    IF NOT FOUND THEN
        INSERT INTO "world"."ui_event_count" (
            "log_date",
            "ui_id",
            "ui_count"
        ) VALUES (
            v_log_date,
            p_ui_id,
            p_ui_count
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadclientini"(
    p_account_id integer
) RETURNS TABLE (
    ini_type smallint,
    ini_contents text
)
LANGUAGE sql
STABLE
AS $function$
    SELECT client_ini."ini_type",
           client_ini."ini_contents"
    FROM "world"."account_client_ini" AS client_ini
    WHERE client_ini."account_id" = p_account_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_saveclientini"(
    p_account_id integer,
    p_ini_type smallint,
    p_ini_contents text
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."account_client_ini" (
        "account_id",
        "ini_type",
        "ini_contents"
    ) VALUES (
        p_account_id,
        p_ini_type,
        p_ini_contents
    )
    ON CONFLICT ("account_id", "ini_type") DO UPDATE
    SET "ini_contents" = EXCLUDED."ini_contents"
$function$;

CREATE TABLE IF NOT EXISTS "world"."user_honors_skill" (
    "char_id" integer NOT NULL,
    "skill_id" integer NOT NULL,
    "skill_level" integer NOT NULL,
    CONSTRAINT "pk_user_honors_skill"
        PRIMARY KEY ("char_id", "skill_id")
);

CREATE OR REPLACE FUNCTION "world"."lin_gethonorsskill"(
    p_character_id integer
) RETURNS TABLE (
    skill_id integer,
    skill_level integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT honors."skill_id",
           honors."skill_level"
    FROM "world"."user_honors_skill" AS honors
    WHERE honors."char_id" = p_character_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_sethonorsskill"(
    p_character_id integer,
    p_skill_id integer,
    p_skill_level integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."user_honors_skill" (
        "char_id",
        "skill_id",
        "skill_level"
    ) VALUES (
        p_character_id,
        p_skill_id,
        p_skill_level
    )
    ON CONFLICT ("char_id", "skill_id") DO UPDATE
    SET "skill_level" = EXCLUDED."skill_level"
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_resethonorsskill"(
    p_character_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."user_honors_skill" AS honors
    WHERE honors."char_id" = p_character_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_delhonorsskill"(
    p_character_id integer,
    p_skill_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."user_honors_skill" AS honors
    WHERE honors."char_id" = p_character_id
      AND honors."skill_id" = p_skill_id
$function$;

CREATE TABLE IF NOT EXISTS "world"."user_wedding" (
    "character_id" integer NOT NULL,
    "spouse_character_id" integer NOT NULL,
    CONSTRAINT "pk_user_wedding" PRIMARY KEY ("character_id")
);

CREATE OR REPLACE FUNCTION "world"."lin_startwedding"(
    p_first_character_id integer,
    p_second_character_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    INSERT INTO "world"."user_wedding" (
        "character_id",
        "spouse_character_id"
    ) VALUES (
        p_first_character_id,
        p_second_character_id
    )
    ON CONFLICT ("character_id") DO UPDATE
    SET "spouse_character_id" = EXCLUDED."spouse_character_id";

    INSERT INTO "world"."user_wedding" (
        "character_id",
        "spouse_character_id"
    ) VALUES (
        p_second_character_id,
        p_first_character_id
    )
    ON CONFLICT ("character_id") DO UPDATE
    SET "spouse_character_id" = EXCLUDED."spouse_character_id";
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_endwedding"(
    p_first_character_id integer,
    p_second_character_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."user_wedding" AS wedding
    WHERE (wedding."character_id" = p_first_character_id
           AND wedding."spouse_character_id" = p_second_character_id)
       OR (wedding."character_id" = p_second_character_id
           AND wedding."spouse_character_id" = p_first_character_id)
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadwedding"(
    p_character_id integer
) RETURNS TABLE (
    spouse_character_id integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT wedding."spouse_character_id"
    FROM "world"."user_wedding" AS wedding
    WHERE wedding."character_id" = p_character_id
$function$;

COMMIT;
