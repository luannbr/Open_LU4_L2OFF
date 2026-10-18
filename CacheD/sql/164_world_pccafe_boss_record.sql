-- Current Cached.exe RQ_EX slots 33-46: siege counters, home PCCafe,
-- academy creation and boss-record persistence.
--
-- The current image fixes every callable ABI and result-column order.  The
-- compatible Classic 3.0/287/Ertheia/H5 bodies are used only for relation
-- effects after those current contracts have been pinned.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_getpledgekilldeathcount"(
    p_castle_id integer
) RETURNS TABLE(
    pledge_id integer,
    kill_count integer,
    death_count integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        pledge."pledge_id",
        pledge."siege_kill",
        pledge."siege_death"
    FROM "world"."pledge" AS pledge
    WHERE pledge."pledge_id" IN (
        SELECT war."pledge_id"
        FROM "world"."castle_war" AS war
        WHERE war."castle_id" = p_castle_id
          AND war."type" IN (1, 2)
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadaccounthomepccafepoint"(
    p_account_id integer
) RETURNS TABLE(
    point integer,
    saved_day integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT home."point", home."saved_day"
    FROM "world"."account_user_home_pccafe_point" AS home
    WHERE home."account_id" = p_account_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_saveaccounthomepccafepoint"(
    p_account_id integer,
    p_point integer,
    p_saved_day integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF p_point < 0 THEN
        RAISE EXCEPTION
            'Not valid parameter : account id[%] point[%]',
            p_account_id,
            p_point;
    END IF;

    UPDATE "world"."account_user_home_pccafe_point" AS home
       SET "point" = p_point,
           "saved_day" = p_saved_day
     WHERE home."account_id" = p_account_id;

    IF NOT FOUND THEN
        INSERT INTO "world"."account_user_home_pccafe_point" (
            "account_id",
            "point",
            "saved_day"
        ) VALUES (
            p_account_id,
            p_point,
            p_saved_day
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatebossrecord"(
    p_character_id integer,
    p_npc_class_id integer,
    p_point integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."user_bossrecord" AS record (
        "char_id",
        "npc_class_id",
        "point",
        "accumulated_point"
    ) VALUES (
        p_character_id,
        p_npc_class_id,
        p_point,
        p_point
    )
    ON CONFLICT ("char_id", "npc_class_id") DO UPDATE
       SET "point" = record."point" + EXCLUDED."point",
           "accumulated_point" =
               record."accumulated_point" + EXCLUDED."accumulated_point";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadbossrecord"(
    p_character_id integer
) RETURNS TABLE(
    npc_class_id integer,
    point integer,
    accumulated_point integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        record."npc_class_id",
        record."point",
        record."accumulated_point"
    FROM "world"."user_bossrecord" AS record
    WHERE record."char_id" = p_character_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_createacademy"(
    p_pledge_id integer,
    p_name text
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF p_name LIKE ' ' THEN
        RAISE EXCEPTION 'academy name has space : name = [%]', p_name;
    END IF;

    IF EXISTS (
        SELECT 1
        FROM "world"."user_prohibit" AS prohibited
        WHERE "world"."mssql_text_eq"(prohibited."char_name", p_name)
    ) THEN
        RAISE EXCEPTION 'academy name is prohibited: name = [%]', p_name;
    END IF;

    IF EXISTS (
        SELECT 1
        FROM "world"."user_prohibit_word" AS prohibited
        WHERE position(lower(prohibited."words") in lower(p_name)) > 0
    ) THEN
        RAISE EXCEPTION
            'academy name has prohibited word: name = [%]',
            p_name;
    END IF;

    INSERT INTO "world"."academy" (
        "pledge_id",
        "name"
    ) VALUES (
        p_pledge_id,
        p_name
    );
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadbossrecordranking"()
RETURNS TABLE(
    character_id integer,
    point integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        record."char_id",
        sum(record."point")::integer
    FROM "world"."user_bossrecord" AS record
    GROUP BY record."char_id"
    HAVING sum(record."point") > 0;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_movetonextbossrecordround"(
    p_round_number integer,
    p_start_time double precision,
    p_end_time integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."bossrecord_round" AS current_round
        WHERE current_round."round_number" = p_round_number
    ) THEN
        INSERT INTO "world"."user_bossrecord_history" (
            "round_number",
            "char_id",
            "point",
            "accumulated_point"
        )
        SELECT
            p_round_number,
            record."char_id",
            sum(record."point")::integer,
            sum(record."accumulated_point")::integer
        FROM "world"."user_bossrecord" AS record
        GROUP BY record."char_id";

        UPDATE "world"."user_bossrecord"
           SET "point" = 0;

        INSERT INTO "world"."bossrecord_round" (
            "round_number",
            "start_time",
            "end_time"
        ) VALUES (
            p_round_number + 1,
            p_start_time::integer,
            p_end_time
        );
    END IF;
END;
$function$;

COMMIT;
