-- Extended requests 101-109. Current Cached.exe fixes every callable ABI.
-- all four substantive full-arity donor families agree on the relational
-- bodies and effects below. RequestPayFortressRent is an in-memory warehouse
-- transfer and therefore adds no SQL surface. RequestLoadAllAuction reuses
-- the exact seven-result function from migration 003.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadfortresssiegeregistry"(
    p_fortress_id integer
) RETURNS TABLE(
    pledge_id integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT registry."pledge_id"
    FROM "world"."fortress_siege_registry" AS registry
    WHERE registry."fortress_id" = p_fortress_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatefortressowner"(
    p_id integer,
    p_pledge_id integer,
    p_last_owner_changed_time integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."fortress"
    SET "pledge_id" = p_pledge_id,
        "last_owner_changed_time" = p_last_owner_changed_time
    WHERE "id" = p_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savefortressstate"(
    p_id integer,
    p_state integer,
    p_state_changed_time integer,
    p_state_elapsed_time integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."fortress"
    SET "state" = p_state,
        "state_changed_time" = p_state_changed_time,
        "state_elapsed_time" = p_state_elapsed_time
    WHERE "id" = p_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savefortresssiegestate"(
    p_fortress_id integer,
    p_siege_status integer,
    p_siege_start_time integer,
    p_siege_elapsed_time integer,
    p_is_valid_info integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."fortress_siege"
    SET "siege_status" = p_siege_status,
        "siege_start_time" = p_siege_start_time,
        "siege_elapsed_time" = p_siege_elapsed_time,
        "is_valid_info" = p_is_valid_info
    WHERE "fortress_id" = p_fortress_id;

    IF NOT FOUND THEN
        INSERT INTO "world"."fortress_siege" (
            "fortress_id",
            "siege_status",
            "siege_start_time",
            "siege_elapsed_time",
            "is_valid_info"
        ) VALUES (
            p_fortress_id,
            p_siege_status,
            p_siege_start_time,
            p_siege_elapsed_time,
            p_is_valid_info
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savefortresscontractinfo"(
    p_id integer,
    p_contract_status integer,
    p_parent_castle_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."fortress"
    SET "contract_status" = p_contract_status,
        "parent_castle_id" = p_parent_castle_id
    WHERE "id" = p_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savefortressrewardinfo"(
    p_id integer,
    p_last_reward_given_time integer,
    p_owner_reward_cycle_count integer,
    p_castle_treasure_level integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."fortress"
    SET "last_reward_given_time" = p_last_reward_given_time,
        "owner_reward_cycle_count" = p_owner_reward_cycle_count,
        "castle_treasure_level" = p_castle_treasure_level
    WHERE "id" = p_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_resetfortresssiegeregistry"(
    p_fortress_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."fortress_siege_registry"
    WHERE "fortress_id" = p_fortress_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savefortresssiegeregistry"(
    p_fortress_id integer,
    p_pledge_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."fortress_siege_registry" (
        "fortress_id",
        "pledge_id"
    )
    SELECT p_fortress_id, p_pledge_id
    WHERE NOT EXISTS (
        SELECT 1
        FROM "world"."fortress_siege_registry" AS registry
        WHERE registry."fortress_id" = p_fortress_id
          AND registry."pledge_id" = p_pledge_id
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savefortresssiegeregisterpledge"(
    p_fortress_id integer,
    p_pledge_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."fortress_siege_registry" (
        "fortress_id",
        "pledge_id"
    )
    SELECT p_fortress_id, p_pledge_id
    WHERE NOT EXISTS (
        SELECT 1
        FROM "world"."fortress_siege_registry" AS registry
        WHERE registry."fortress_id" = p_fortress_id
          AND registry."pledge_id" = p_pledge_id
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savefortresssiegeunregisterpledge"(
    p_fortress_id integer,
    p_pledge_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."fortress_siege_registry"
    WHERE "fortress_id" = p_fortress_id
      AND "pledge_id" = p_pledge_id;
$function$;

COMMIT;
