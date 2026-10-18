-- Current Cached.exe CPledgeJoinDB persistence ABI used by startup and the
-- in-memory registration/time caches.
--
-- Input/output order is fixed by current machine code. Relation effects are
-- corroborated by byte-equivalent Classic 3.0, modified-287 and Ertheia SQL
-- procedures. Intentionally odd missing world conditions are kept.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_initializepledgewaitlist"(
    p_now_time integer,
    p_init_time integer,
    p_world_id integer
) RETURNS TABLE("char_id" integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    RETURN QUERY
        SELECT waiting."char_id"
        FROM "world"."pledge_recruit_waiting_list" AS waiting
        WHERE (waiting."register_date" < (p_now_time - p_init_time)
               AND waiting."world_id" = p_world_id)
           OR waiting."pledge_id" IN (
                SELECT board."pledge_id"
                FROM "world"."pledge_recruit_board" AS board
                WHERE board."register_date" < (p_now_time - p_init_time)
                  AND board."world_id" = p_world_id
           );

    DELETE FROM "world"."pledge_recruit_waiting_list" AS waiting
    WHERE (waiting."register_date" < (p_now_time - p_init_time)
           AND waiting."world_id" = p_world_id)
       OR waiting."pledge_id" IN (
            SELECT board."pledge_id"
            FROM "world"."pledge_recruit_board" AS board
            WHERE board."register_date" < (p_now_time - p_init_time)
              AND board."world_id" = p_world_id
       );
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_initializepledgerecruitboard"(
    p_now_time integer,
    p_init_time integer,
    p_world_id integer
) RETURNS TABLE("pledge_id" integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    current_pledge_id integer;
BEGIN
    RETURN QUERY
        SELECT board."pledge_id"
        FROM "world"."pledge_recruit_board" AS board
        WHERE board."register_date" < (p_now_time - p_init_time)
          AND board."world_id" = p_world_id;

    DELETE FROM "world"."pledge_recruit_board" AS board
    WHERE board."register_date" < (p_now_time - p_init_time)
      AND board."world_id" = p_world_id;

    -- The donor cursor has no world filter and revisits a pledge
    -- once per remaining board row. Keep that operation order instead of
    -- silently narrowing it to p_world_id.
    FOR current_pledge_id IN
        SELECT board."pledge_id"
        FROM "world"."pledge_recruit_board" AS board
    LOOP
        UPDATE "world"."pledge_recruit_board" AS board
        SET "pledge_namevalue" = (
            SELECT pledge."root_name_value"
            FROM "world"."pledge" AS pledge
            WHERE pledge."pledge_id" = current_pledge_id
        )
        WHERE board."pledge_id" = current_pledge_id;

        UPDATE "world"."pledge_recruit_board" AS board
        SET "pledge_level" = (
            SELECT pledge."skill_level"::integer
            FROM "world"."pledge" AS pledge
            WHERE pledge."pledge_id" = current_pledge_id
        )
        WHERE board."pledge_id" = current_pledge_id;
    END LOOP;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_initializepledgedraftlist"(
    p_now_time integer,
    p_init_time integer,
    p_world_id integer
) RETURNS TABLE("char_id" integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    RETURN QUERY
        SELECT draft."char_id"
        FROM "world"."pledge_draft_user_list" AS draft
        WHERE draft."register_date" < (p_now_time - p_init_time)
          AND draft."world_id" = p_world_id;

    DELETE FROM "world"."pledge_draft_user_list" AS draft
    WHERE draft."register_date" < (p_now_time - p_init_time)
      AND draft."world_id" = p_world_id;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_confirmdraftuserlist"(
    p_world_id integer,
    p_char_id integer
) RETURNS TABLE("count" integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT count(*)::integer
    FROM "world"."pledge_draft_user_list" AS draft
    WHERE draft."world_id" = p_world_id
      AND draft."char_id" = p_char_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_confirmpledgerecruitboard"(
    p_world_id integer,
    p_pledge_id integer
) RETURNS TABLE("count" integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT count(*)::integer
    FROM "world"."pledge_recruit_board" AS board
    WHERE board."world_id" = p_world_id
      AND board."pledge_id" = p_pledge_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_confirmpledgeuserlist"(
    p_world_id integer,
    p_char_id integer
) RETURNS TABLE("count" integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT count(*)::integer
    FROM "world"."pledge_recruit_waiting_list" AS waiting
    WHERE waiting."world_id" = p_world_id
      AND waiting."char_id" = p_char_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_countpledgewaitinguser"(
    p_world_id integer,
    p_pledge_id integer
) RETURNS TABLE("count" integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT count(*)::integer
    FROM "world"."pledge_recruit_waiting_list" AS waiting
    WHERE waiting."world_id" = p_world_id
      AND waiting."pledge_id" = p_pledge_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_gettimeonpledgeboard"(
    p_world_id integer,
    p_pledge_id integer
) RETURNS TABLE("register_date" integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT board."register_date"
    FROM "world"."pledge_recruit_board" AS board
    WHERE board."pledge_id" = p_pledge_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_gettimeonpledgedraftlist"(
    p_world_id integer,
    p_char_id integer
) RETURNS TABLE("register_date" integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT draft."register_date"
    FROM "world"."pledge_draft_user_list" AS draft
    WHERE draft."char_id" = p_char_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_gettimeonpledgewaitlist"(
    p_world_id integer,
    p_char_id integer
) RETURNS TABLE("register_date" integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT waiting."register_date"
    FROM "world"."pledge_recruit_waiting_list" AS waiting
    WHERE waiting."char_id" = p_char_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_modifypledgedraftuser"(
    p_world_id integer,
    p_char_id integer,
    p_level integer,
    p_class integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."pledge_draft_user_list" AS draft
    SET "level" = p_level,
        "class" = p_class
    WHERE draft."world_id" = p_world_id
      AND draft."char_id" = p_char_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_modifypledgewaitinguser"(
    p_world_id integer,
    p_char_id integer,
    p_level integer,
    p_class integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."pledge_recruit_waiting_list" AS waiting
    SET "level" = p_level,
        "class" = p_class
    WHERE waiting."world_id" = p_world_id
      AND waiting."char_id" = p_char_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_searchpledgewaitinglist"(
    p_world_id integer,
    p_pledge_id integer
) RETURNS TABLE(
    "char_id" integer,
    "name" text,
    "class" integer,
    "level" integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT waiting."char_id",
           waiting."name"::text,
           waiting."class",
           waiting."level"
    FROM "world"."pledge_recruit_waiting_list" AS waiting
    WHERE waiting."world_id" = p_world_id
      AND waiting."pledge_id" = p_pledge_id
    ORDER BY waiting."register_date" DESC
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletepledgerecruitboard"(
    p_world_id integer,
    p_pledge_id integer
) RETURNS TABLE("char_id" integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    -- The donor SELECT compares @world_id with itself and therefore returns
    -- this pledge's character IDs across worlds. The two DELETEs below do use
    -- the world column. Preserve that asymmetry exactly.
    RETURN QUERY
        SELECT waiting."char_id"
        FROM "world"."pledge_recruit_waiting_list" AS waiting
        WHERE waiting."pledge_id" = p_pledge_id;

    DELETE FROM "world"."pledge_recruit_board" AS board
    WHERE board."pledge_id" = p_pledge_id
      AND board."world_id" = p_world_id;

    DELETE FROM "world"."pledge_recruit_waiting_list" AS waiting
    WHERE waiting."pledge_id" = p_pledge_id
      AND waiting."world_id" = p_world_id;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getpledgeonpledgerecruitboard"()
RETURNS TABLE("pledge_id" integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT board."pledge_id"
    FROM "world"."pledge_recruit_board" AS board
    ORDER BY board."pledge_namevalue"
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getlatestapplieduseronpledge"(
    p_world_id integer
) RETURNS TABLE("pledge_id" integer, "register_date" integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT waiting."pledge_id",
           max(waiting."register_date")::integer
    FROM "world"."pledge_recruit_waiting_list" AS waiting
    GROUP BY waiting."pledge_id"
$function$;

COMMIT;
