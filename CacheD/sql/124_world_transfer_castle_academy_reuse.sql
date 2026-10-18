-- Extended requests 81-83 and 87-88. Current Cached.exe fixes each callable
-- ABI. Classic 3.0, Ertheia and modified-287 donors agree on these bodies.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_saveclancommtransferresult"(
    p_pledge_id integer,
    p_result integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."pledge_master_transfer" AS transfer
    SET "status" = p_result,
        "status_time" = "world"."mssql_getdate"()
    WHERE transfer."status" = 1
      AND transfer."pledge_id" = p_pledge_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setpreviouscastleownerid"(
    p_residence_id integer,
    p_pledge_id integer,
    p_world_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."castle" AS castle
    SET "prev_castle_owner_id" = p_pledge_id,
        "oldworld_id" = p_world_id
    WHERE castle."id" = p_residence_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_academygraduatelist"(
    p_pledge_id integer
) RETURNS TABLE(char_name text)
LANGUAGE sql
STABLE
AS $function$
    SELECT member_data."char_name"::text
    FROM "world"."user_data" AS member_data
    JOIN "world"."academy_member" AS academy_member
      ON academy_member."user_id" = member_data."char_id"
    WHERE academy_member."pledge_id" = p_pledge_id
      AND academy_member."status" = 1
      AND academy_member."status_time"
          BETWEEN "world"."mssql_getdate"() - interval '7 days'
              AND "world"."mssql_getdate"();
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadsharedreusedelaysofitems"(
    p_char_id integer
) RETURNS TABLE(
    shared_delay_id integer,
    next_available_time integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT reuse."shared_delay_id",
           reuse."next_available_time"
    FROM "world"."shared_reuse_delays_of_items" AS reuse
    WHERE reuse."char_id" = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savesharedreusedelayofitem"(
    p_char_id integer,
    p_shared_delay_id integer,
    p_next_available_time integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    DELETE FROM "world"."shared_reuse_delays_of_items" AS reuse
    WHERE reuse."char_id" = p_char_id
      AND reuse."shared_delay_id" = p_shared_delay_id;

    INSERT INTO "world"."shared_reuse_delays_of_items" (
        "char_id",
        "shared_delay_id",
        "next_available_time"
    ) VALUES (
        p_char_id,
        p_shared_delay_id,
        p_next_available_time
    );
END;
$function$;

COMMIT;
